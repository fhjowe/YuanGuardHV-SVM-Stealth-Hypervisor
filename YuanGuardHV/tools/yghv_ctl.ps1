<#
.SYNOPSIS
YuanGuardHV control-device client (IOCTL validation tool).

Commands:
  state
  set-target <pid>
  add-page <hex_va>
  remove-page <hex_va>
  start
  stop
  target
  list-targets
  list-pages
  list-hooks
  install-hook <name|hex_va> [hook_id]
  remove-hook <hook_id>
  clear
  config [auto-disarm <0|1> | deny-status <hex>]
  set-auto-start
  unset-auto-start
  harden-service
  unharden-service
  selftest
  exit-test
#>
param(
    [Parameter(Position = 0)][string]$Command = 'state',
    [Parameter(Position = 1)]$Arg1 = $null,
    [Parameter(Position = 2)]$Arg2 = $null
)

$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class YghvCtlNative
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr CreateFile(
        string lpFileName,
        uint dwDesiredAccess,
        uint dwShareMode,
        IntPtr lpSecurityAttributes,
        uint dwCreationDisposition,
        uint dwFlagsAndAttributes,
        IntPtr hTemplateFile);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool CloseHandle(IntPtr hObject);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool DeviceIoControl(
        IntPtr hDevice,
        uint dwIoControlCode,
        byte[] lpInBuffer,
        uint nInBufferSize,
        byte[] lpOutBuffer,
        uint nOutBufferSize,
        out uint lpBytesReturned,
        IntPtr lpOverlapped);

    [DllImport("kernel32.dll")]
    public static extern uint GetCurrentProcessId();

    public static uint IoCtl(uint fn)
    {
        return (0x5947u << 16) | (0u << 14) | (fn << 2) | 0u;
    }

    public const uint GENERIC_READ = 0x80000000;
    public const uint GENERIC_WRITE = 0x40000000;
    public const uint FILE_SHARE_READ = 0x1;
    public const uint FILE_SHARE_WRITE = 0x2;
    public const uint OPEN_EXISTING = 3;
}

public static class YghvPrivilege
{
    [StructLayout(LayoutKind.Sequential)]
    public struct Luid
    {
        public uint LowPart;
        public int HighPart;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct TokenPrivileges
    {
        public uint PrivilegeCount;
        public Luid Luid;
        public uint Attributes;
    }

    [DllImport("kernel32.dll")]
    public static extern IntPtr GetCurrentProcess();

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool CloseHandle(IntPtr handle);

    [DllImport("advapi32.dll", SetLastError = true)]
    public static extern bool OpenProcessToken(
        IntPtr processHandle,
        uint desiredAccess,
        out IntPtr tokenHandle);

    [DllImport("advapi32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern bool LookupPrivilegeValue(
        string systemName,
        string name,
        out Luid luid);

    [DllImport("advapi32.dll", SetLastError = true)]
    public static extern bool AdjustTokenPrivileges(
        IntPtr tokenHandle,
        bool disableAllPrivileges,
        ref TokenPrivileges newState,
        uint bufferLength,
        IntPtr previousState,
        IntPtr returnLength);

    public static bool EnableSeDebugPrivilege()
    {
        IntPtr token;
        if (!OpenProcessToken(GetCurrentProcess(), 0x0028, out token))
            return false;
        try
        {
            Luid luid;
            if (!LookupPrivilegeValue(null, "SeDebugPrivilege", out luid))
                return false;

            TokenPrivileges tp = new TokenPrivileges();
            tp.PrivilegeCount = 1;
            tp.Luid = luid;
            tp.Attributes = 0x2; /* SE_PRIVILEGE_ENABLED */

            if (!AdjustTokenPrivileges(token, false, ref tp, 0, IntPtr.Zero, IntPtr.Zero))
                return false;
            return Marshal.GetLastWin32Error() == 0;
        }
        finally
        {
            CloseHandle(token);
        }
    }
}
'@

if (-not [YghvPrivilege]::EnableSeDebugPrivilege()) {
    throw 'Unable to enable SeDebugPrivilege. Run this script from an elevated PowerShell session.'
}

$sysCommands = @('set-auto-start', 'unset-auto-start', 'harden-service', 'unharden-service')
if ($sysCommands -contains $Command.ToLower()) {
    switch ($Command.ToLower()) {
        'set-auto-start' {
            & sc.exe config yuanguard start= auto | Out-Null
            if ($LASTEXITCODE -ne 0) {
                throw ("set-auto-start: sc config failed rc={0}" -f $LASTEXITCODE)
            }
            Write-Host 'set-auto-start: OK (start=auto)'
            exit 0
        }
        'unset-auto-start' {
            & sc.exe config yuanguard start= demand | Out-Null
            if ($LASTEXITCODE -ne 0) {
                throw ("unset-auto-start: sc config failed rc={0}" -f $LASTEXITCODE)
            }
            Write-Host 'unset-auto-start: OK (start=demand)'
            exit 0
        }
        'harden-service' {
            $backup = (& sc.exe sdshow yuanguard | Select-Object -Last 1).Trim()
            if (-not $backup) {
                throw 'harden-service: sdshow returned nothing'
            }
            Set-Content -LiteralPath 'D:\aaaaaavm\yghv_service_sddl_backup.txt' `
                -Value $backup -Encoding UTF8
            $hardenSddl = 'D:(D;;SDWP;;;BA)(A;;CCLCSWRPWPDTLOCRRC;;;SY)' +
                '(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;BA)' +
                '(A;;CCLCSWLOCRRC;;;IU)(A;;CCLCSWLOCRRC;;;SU)' +
                'S:(AU;FA;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;WD)'
            $out = & sc.exe sdset yuanguard $hardenSddl
            if (($out -join ' ') -notmatch 'SUCCESS') {
                throw ("harden-service: sdset failed: {0}" -f ($out -join ' '))
            }
            Write-Host 'harden-service: OK (service delete denied, backup saved)'
            exit 0
        }
        'unharden-service' {
            $backupPath = 'D:\aaaaaavm\yghv_service_sddl_backup.txt'
            if (-not (Test-Path $backupPath)) {
                throw 'unharden-service: backup SDDL not found'
            }
            $backup = (Get-Content -LiteralPath $backupPath -Raw).Trim()
            $out = & sc.exe sdset yuanguard $backup
            if (($out -join ' ') -notmatch 'SUCCESS') {
                throw ("unharden-service: sdset failed: {0}" -f ($out -join ' '))
            }
            Write-Host 'unharden-service: OK (backup SDDL restored)'
            exit 0
        }
    }
}

$devicePath = '\\.\YuanGuardHV'
$handle = [YghvCtlNative]::CreateFile(
    $devicePath,
    [YghvCtlNative]::GENERIC_READ -bor [YghvCtlNative]::GENERIC_WRITE,
    [YghvCtlNative]::FILE_SHARE_READ -bor [YghvCtlNative]::FILE_SHARE_WRITE,
    [IntPtr]::Zero,
    [YghvCtlNative]::OPEN_EXISTING,
    0,
    [IntPtr]::Zero)
if ($handle -eq [IntPtr]::Zero) {
    throw ("CreateFile {0} failed, Win32 error 0x{1:X8}" -f $devicePath,
        [Runtime.InteropServices.Marshal]::GetLastWin32Error())
}
if ($handle -eq [IntPtr](-1)) {
    throw ("CreateFile {0} failed, Win32 error 0x{1:X8}" -f $devicePath,
        [Runtime.InteropServices.Marshal]::GetLastWin32Error())
}

function Invoke-YghvIoctl {
    param([uint32]$Code, [byte[]]$InBytes = $null, [uint32]$OutputLength = 0)
    if ($null -eq $InBytes) { $InBytes = New-Object byte[] 0 }
    $out = New-Object byte[] $OutputLength
    $returned = [uint32]0
    $ok = [YghvCtlNative]::DeviceIoControl(
        $handle, $Code, $InBytes, [uint32]$InBytes.Length,
        $out, $OutputLength, [ref]$returned, [IntPtr]::Zero)
    if (-not $ok) {
        throw ("DeviceIoControl 0x{0:X8} failed, Win32 error 0x{1:X8}" -f
            $Code, [Runtime.InteropServices.Marshal]::GetLastWin32Error())
    }
    return $out
}

function Read-YghvState {
    $out = Invoke-YghvIoctl -Code ([YghvCtlNative]::IoCtl(0x805)) -OutputLength 12
    return @{
        active    = [BitConverter]::ToUInt32($out, 0)
        pid       = [BitConverter]::ToUInt32($out, 4)
        pageCount = [BitConverter]::ToUInt32($out, 8)
    }
}

function Read-YghvTarget {
    $out = Invoke-YghvIoctl -Code ([YghvCtlNative]::IoCtl(0x806)) -OutputLength 24
    return @{
        active    = [BitConverter]::ToUInt32($out, 0)
        pid       = [BitConverter]::ToUInt32($out, 4)
        pageCount = [BitConverter]::ToUInt32($out, 8)
        hookCount = [BitConverter]::ToUInt32($out, 12)
        cr3       = [BitConverter]::ToUInt64($out, 16)
    }
}

function Read-YghvPages {
    $buf = New-Object byte[] 1544
    [BitConverter]::GetBytes([uint32]64).CopyTo($buf, 0)
    $out = Invoke-YghvIoctl -Code ([YghvCtlNative]::IoCtl(0x807)) `
        -InBytes $buf -OutputLength 1544
    $returned = [BitConverter]::ToUInt32($out, 4)
    $pages = @()
    for ($i = 0; $i -lt [Math]::Min($returned, 64); $i++) {
        $base = 8 + $i * 24
        $pages += [pscustomobject]@{
            gpa       = ('0x{0:X}' -f [BitConverter]::ToUInt64($out, $base))
            targetVa  = ('0x{0:X}' -f [BitConverter]::ToUInt64($out, $base + 8))
            flags     = $out[$base + 16]
            armed     = $out[$base + 17]
        }
    }
    return @{ returned = $returned; pages = $pages }
}

function Read-YghvHooks {
    $buf = New-Object byte[] 104
    [BitConverter]::GetBytes([uint32]4).CopyTo($buf, 0)
    $out = Invoke-YghvIoctl -Code ([YghvCtlNative]::IoCtl(0x808)) `
        -InBytes $buf -OutputLength 104
    $returned = [BitConverter]::ToUInt32($out, 4)
    $hooks = @()
    for ($i = 0; $i -lt $returned; $i++) {
        $base = 8 + $i * 24
        $hooks += [pscustomobject]@{
            funcVa   = ('0x{0:X}' -f [BitConverter]::ToUInt64($out, $base))
            hookId   = [BitConverter]::ToUInt32($out, $base + 8)
            installed = [BitConverter]::ToUInt32($out, $base + 12)
            patchLen = [BitConverter]::ToUInt32($out, $base + 16)
        }
    }
    return @{ returned = $returned; hooks = $hooks }
}

function Read-YghvTargets {
    $buf = New-Object byte[] 104
    [BitConverter]::GetBytes([uint32]4).CopyTo($buf, 0)
    $out = Invoke-YghvIoctl -Code ([YghvCtlNative]::IoCtl(0x80E)) `
        -InBytes $buf -OutputLength 104
    $returned = [BitConverter]::ToUInt32($out, 4)
    $targets = @()
    for ($i = 0; $i -lt $returned; $i++) {
        $base = 8 + $i * 24
        $targets += [pscustomobject]@{
            active    = [BitConverter]::ToUInt32($out, $base)
            pid       = [BitConverter]::ToUInt32($out, $base + 4)
            pageCount = [BitConverter]::ToUInt32($out, $base + 8)
            hookCount = [BitConverter]::ToUInt32($out, $base + 12)
            cr3       = [BitConverter]::ToUInt64($out, $base + 16)
        }
    }
    return @{ returned = $returned; targets = $targets }
}

function Read-YghvConfig {
    $out = Invoke-YghvIoctl -Code ([YghvCtlNative]::IoCtl(0x80B)) -OutputLength 8
    return @{
        autoDisarm = [BitConverter]::ToUInt32($out, 0)
        denyStatus = [BitConverter]::ToUInt32($out, 4)
    }
}

function New-YghvHookInstallBuffer {
    param([string]$NameOrVa, [uint32]$HookId)
    $buf = New-Object byte[] 144
    [BitConverter]::GetBytes($HookId).CopyTo($buf, 0)
    if ($NameOrVa -match '^0x') {
        $va = [Convert]::ToUInt64($NameOrVa.Substring(2), 16)
        [BitConverter]::GetBytes([uint64]$va).CopyTo($buf, 8)
    } else {
        if ($NameOrVa.Length -gt 63) {
            throw 'install-hook: name too long (max 63 chars)'
        }
        $bytes = [System.Text.Encoding]::Unicode.GetBytes($NameOrVa)
        $bytes.CopyTo($buf, 16)
    }
    return $buf
}

try {
    switch ($Command.ToLower()) {
        'state' {
            $st = Read-YghvState
            Write-Host ("state: active={0} pid={1} page_count={2}" -f
                $st.active, $st.pid, $st.pageCount)
        }
        'lasthit' {
            # 9.258 (206-C3): last protection verdict hit (exact faulting gpa)
            $out = Invoke-YghvIoctl -Code ([YghvCtlNative]::IoCtl(0x80F)) -OutputLength 8
            $hit = [BitConverter]::ToUInt64($out, 0)
            Write-Host ("lasthit: 0x{0:X}" -f $hit)
        }
        'set-target' {
            $pidVal = [uint32]::Parse($Arg1)
            Invoke-YghvIoctl -Code ([YghvCtlNative]::IoCtl(0x800)) `
                -InBytes ([BitConverter]::GetBytes($pidVal)) | Out-Null
            Write-Host ("set-target: pid={0} OK" -f $pidVal)
        }
        'add-page' {
            $va = [Convert]::ToUInt64($Arg1, 16)
            Invoke-YghvIoctl -Code ([YghvCtlNative]::IoCtl(0x801)) `
                -InBytes ([BitConverter]::GetBytes($va)) | Out-Null
            Write-Host ("add-page: va=0x{0:X} OK" -f $va)
        }
        'remove-page' {
            $va = [Convert]::ToUInt64($Arg1, 16)
            Invoke-YghvIoctl -Code ([YghvCtlNative]::IoCtl(0x802)) `
                -InBytes ([BitConverter]::GetBytes($va)) | Out-Null
            Write-Host ("remove-page: va=0x{0:X} OK" -f $va)
        }
        'start' {
            Invoke-YghvIoctl -Code ([YghvCtlNative]::IoCtl(0x803)) | Out-Null
            Write-Host 'start: OK'
        }
        'stop' {
            Invoke-YghvIoctl -Code ([YghvCtlNative]::IoCtl(0x804)) | Out-Null
            Write-Host 'stop: OK'
        }
        'target' {
            $t = Read-YghvTarget
            Write-Host ("target: active={0} pid={1} cr3=0x{2:X} page_count={3} hook_count={4}" -f
                $t.active, $t.pid, $t.cr3, $t.pageCount, $t.hookCount)
        }
        'list-targets' {
            $r = Read-YghvTargets
            Write-Host ("list-targets: returned={0}" -f $r.returned)
            foreach ($t in $r.targets) {
                Write-Host ("  active={0} pid={1} cr3=0x{2:X} pages={3} hooks={4}" -f
                    $t.active, $t.pid, $t.cr3, $t.pageCount, $t.hookCount)
            }
        }
        'list-pages' {
            $r = Read-YghvPages
            Write-Host ("list-pages: returned={0}" -f $r.returned)
            foreach ($p in $r.pages) {
                Write-Host ("  gpa={0} va={1} flags={2} armed={3}" -f
                    $p.gpa, $p.targetVa, $p.flags, $p.armed)
            }
        }
        'list-hooks' {
            $r = Read-YghvHooks
            Write-Host ("list-hooks: returned={0}" -f $r.returned)
            foreach ($h in $r.hooks) {
                Write-Host ("  id={0} va={1} installed={2} patch_len={3}" -f
                    $h.hookId, $h.funcVa, $h.installed, $h.patchLen)
            }
        }
        'install-hook' {
            $hid = if ($null -eq $Arg2) { [uint32]2 } else { [uint32]::Parse($Arg2) }
            $buf = New-YghvHookInstallBuffer -NameOrVa $Arg1 -HookId $hid
            Invoke-YghvIoctl -Code ([YghvCtlNative]::IoCtl(0x80C)) `
                -InBytes $buf | Out-Null
            Write-Host ("install-hook: id={0} target={1} OK" -f $hid, $Arg1)
        }
        'remove-hook' {
            $hid = [uint32]::Parse($Arg1)
            $buf = New-Object byte[] 8
            [BitConverter]::GetBytes($hid).CopyTo($buf, 0)
            Invoke-YghvIoctl -Code ([YghvCtlNative]::IoCtl(0x80D)) `
                -InBytes $buf | Out-Null
            Write-Host ("remove-hook: id={0} OK" -f $hid)
        }
        'clear' {
            Invoke-YghvIoctl -Code ([YghvCtlNative]::IoCtl(0x809)) | Out-Null
            $st = Read-YghvState
            Write-Host ("clear: OK (active={0} pid={1} page_count={2})" -f
                $st.active, $st.pid, $st.pageCount)
        }
        'config' {
            if ($null -eq $Arg1) {
                $c = Read-YghvConfig
                Write-Host ("config: auto_disarm={0} deny_status=0x{1:X}" -f
                    $c.autoDisarm, $c.denyStatus)
            } elseif ($Arg1.ToLower() -eq 'auto-disarm') {
                $val = [uint32]::Parse($Arg2)
                if ($val -gt 1) { throw 'config: auto-disarm must be 0 or 1' }
                $cfg = New-Object byte[] 8
                [BitConverter]::GetBytes($val).CopyTo($cfg, 0)
                $cur = Read-YghvConfig
                [BitConverter]::GetBytes([uint32]$cur.denyStatus).CopyTo($cfg, 4)
                Invoke-YghvIoctl -Code ([YghvCtlNative]::IoCtl(0x80A)) `
                    -InBytes $cfg | Out-Null
                Write-Host ("config: auto_disarm={0} OK" -f $val)
            } elseif ($Arg1.ToLower() -eq 'deny-status') {
                $val = [uint32]([Convert]::ToUInt64($Arg2, 16))
                if ($val -eq 0) { throw 'config: deny-status must be non-zero' }
                $cfg = New-Object byte[] 8
                $cur = Read-YghvConfig
                [BitConverter]::GetBytes([uint32]$cur.autoDisarm).CopyTo($cfg, 0)
                [BitConverter]::GetBytes($val).CopyTo($cfg, 4)
                Invoke-YghvIoctl -Code ([YghvCtlNative]::IoCtl(0x80A)) `
                    -InBytes $cfg | Out-Null
                Write-Host ("config: deny_status=0x{0:X} OK" -f $val)
            } else {
                throw ("config: unknown option {0}" -f $Arg1)
            }
        }
        'selftest' {
            $bytes = New-Object byte[] 4096
            $gc = [Runtime.InteropServices.GCHandle]::Alloc(
                $bytes, [Runtime.InteropServices.GCHandleType]::Pinned)
            try {
                $addr = $gc.AddrOfPinnedObject().ToInt64()
                $pidVal = [YghvCtlNative]::GetCurrentProcessId()
                Write-Host ("selftest: pid={0} buf_va=0x{1:X}" -f $pidVal, $addr)
                $base = Read-YghvState
                $baseCount = $base.pageCount
                Write-Host ("selftest: baseline page_count={0}" -f $baseCount)

                Invoke-YghvIoctl -Code ([YghvCtlNative]::IoCtl(0x800)) `
                    -InBytes ([BitConverter]::GetBytes([uint32]$pidVal)) | Out-Null
                Write-Host 'selftest: set-target OK'
                $targets = Read-YghvTargets
                $mySlot = $targets.targets | Where-Object { $_.pid -eq $pidVal }
                if ($null -eq $mySlot -or $mySlot.pageCount -ne 0) {
                    throw 'selftest: own target slot not registered/empty'
                }

                Invoke-YghvIoctl -Code ([YghvCtlNative]::IoCtl(0x801)) `
                    -InBytes ([BitConverter]::GetBytes([uint64]$addr)) | Out-Null
                Write-Host 'selftest: add-page OK'
                $targets = Read-YghvTargets
                $mySlot = $targets.targets | Where-Object { $_.pid -eq $pidVal }
                if ($null -eq $mySlot -or $mySlot.pageCount -ne 1) {
                    throw 'selftest: add-page did not register in own slot'
                }
                $pages = Read-YghvPages
                $addrText = '0x{0:X}' -f $addr
                if (-not ($pages.pages.targetVa -contains $addrText)) {
                    throw ("selftest: list-pages mismatch (returned={0})" -f
                        $pages.returned)
                }
                Write-Host 'selftest: list-pages OK'

                Invoke-YghvIoctl -Code ([YghvCtlNative]::IoCtl(0x803)) | Out-Null
                Write-Host 'selftest: start OK'

                $st = Read-YghvState
                if ($st.active -ne 1) {
                    throw ("selftest: not active after start (active={0})" -f
                        $st.active)
                }

                [Runtime.InteropServices.Marshal]::WriteInt64(
                    [IntPtr]$addr, 0x1122334455667788)
                $readback = [Runtime.InteropServices.Marshal]::ReadInt64([IntPtr]$addr)
                if ($readback -ne 0x1122334455667788) {
                    throw 'selftest: write/read mismatch on protected page'
                }
                # 9.258 (206-C3): the verdict must have recorded a hit
                $hitOut = Invoke-YghvIoctl -Code ([YghvCtlNative]::IoCtl(0x80F)) -OutputLength 8
                $lastHit = [BitConverter]::ToUInt64($hitOut, 0)
                if ($lastHit -eq 0) {
                    throw 'selftest: no protection hit recorded (last-hit==0)'
                }
                Write-Host 'selftest: user write/read OK (page armed in NPT)'

                Invoke-YghvIoctl -Code ([YghvCtlNative]::IoCtl(0x804)) | Out-Null
                Invoke-YghvIoctl -Code ([YghvCtlNative]::IoCtl(0x802)) `
                    -InBytes ([BitConverter]::GetBytes([uint64]$addr)) | Out-Null

                $st = Read-YghvState
                if ($st.active -ne 0) {
                    throw ("selftest: still active after stop (active={0})" -f
                        $st.active)
                }
                $pages = Read-YghvPages
                if ($pages.pages.targetVa -contains $addrText) {
                    throw 'selftest: own page still listed after remove'
                }
                Write-Host 'selftest: PASS'
            } finally {
                $gc.Free()
            }
        }
        'selftest-abort' {
            # 9.258 (206-C3): watchdog test — arm a page then die WITHOUT any
            # cleanup (the deliberate c12 leak scenario). The driver's process
            # -exit watchdog must disarm + free the slot; a state probe right
            # after this process exits must show page_count=0 for this pid.
            $bytes = New-Object byte[] 4096
            $gc = [Runtime.InteropServices.GCHandle]::Alloc(
                $bytes, [Runtime.InteropServices.GCHandleType]::Pinned)
            try {
                $addr = $gc.AddrOfPinnedObject().ToInt64()
                $pidVal = [YghvCtlNative]::GetCurrentProcessId()
                Write-Host ("selftest-abort: pid={0} buf_va=0x{1:X}" -f
                    $pidVal, $addr)
                Invoke-YghvIoctl -Code ([YghvCtlNative]::IoCtl(0x800)) `
                    -InBytes ([BitConverter]::GetBytes([uint32]$pidVal)) | Out-Null
                Write-Host 'selftest-abort: set-target OK'
                Invoke-YghvIoctl -Code ([YghvCtlNative]::IoCtl(0x801)) `
                    -InBytes ([BitConverter]::GetBytes([uint64]$addr)) | Out-Null
                Write-Host 'selftest-abort: add-page OK'
                Invoke-YghvIoctl -Code ([YghvCtlNative]::IoCtl(0x803)) | Out-Null
                Write-Host 'selftest-abort: start OK — dying WITHOUT cleanup'
            } finally {
                $gc.Free()
            }
            # process exits here with the page still armed; the driver's
            # watchdog does the disarm+free during teardown
        }
        'exit-test' {
            $child = Start-Process -FilePath 'cmd.exe' `
                -ArgumentList '/c ping -n 3 127.0.0.1 >nul' `
                -PassThru -WindowStyle Hidden
            try {
                $childPid = $child.Id
                Write-Host ("exit-test: child pid={0}" -f $childPid)
                Invoke-YghvIoctl -Code ([YghvCtlNative]::IoCtl(0x800)) `
                    -InBytes ([BitConverter]::GetBytes([uint32]$childPid)) | Out-Null
                Write-Host 'exit-test: set-target OK'

                $targets = Read-YghvTargets
                $childSlot = $targets.targets | Where-Object { $_.pid -eq $childPid }
                if ($null -eq $childSlot) {
                    throw 'exit-test: child slot not registered'
                }

                $deadline = (Get-Date).AddSeconds(20)
                while ((Get-Date) -lt $deadline) {
                    $child.Refresh()
                    if ($child.HasExited) { break }
                    Start-Sleep -Milliseconds 200
                }
                if (-not $child.HasExited) {
                    throw 'exit-test: child did not exit in time'
                }

                $deadline = (Get-Date).AddSeconds(10)
                do {
                    Start-Sleep -Milliseconds 250
                    $targets = Read-YghvTargets
                    $childSlot = $targets.targets |
                        Where-Object { $_.pid -eq $childPid -and $_.cr3 -ne 0 }
                } while ($null -ne $childSlot -and
                    (Get-Date) -lt $deadline)
                $targets = Read-YghvTargets
                $childSlot = $targets.targets |
                    Where-Object { $_.pid -eq $childPid -and $_.cr3 -ne 0 }
                if ($null -ne $childSlot) {
                    throw 'exit-test: child slot auto disarm not observed'
                }
                Write-Host 'exit-test: PASS (child slot auto cleared)'
            } finally {
                if (-not $child.HasExited) {
                    Stop-Process -Id $child.Id -Force -ErrorAction SilentlyContinue
                }
                $child.Dispose()
            }
        }
        default {
            throw ("unknown command: {0}" -f $Command)
        }
    }
} finally {
    if ($handle -ne [IntPtr]::Zero) {
        [YghvCtlNative]::CloseHandle($handle) | Out-Null
        $handle = [IntPtr]::Zero
    }
}
