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
'@

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

try {
    switch ($Command.ToLower()) {
        'state' {
            $st = Read-YghvState
            Write-Host ("state: active={0} pid={1} page_count={2}" -f
                $st.active, $st.pid, $st.pageCount)
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
                $st = Read-YghvState
                if ($st.pageCount -ne 0) {
                    throw ("selftest: set-target did not clear pages (page_count={0})" -f
                        $st.pageCount)
                }

                Invoke-YghvIoctl -Code ([YghvCtlNative]::IoCtl(0x801)) `
                    -InBytes ([BitConverter]::GetBytes([uint64]$addr)) | Out-Null
                Write-Host 'selftest: add-page OK'
                $st = Read-YghvState
                if ($st.pageCount -ne 1) {
                    throw ("selftest: add-page did not register (page_count={0})" -f
                        $st.pageCount)
                }

                Invoke-YghvIoctl -Code ([YghvCtlNative]::IoCtl(0x803)) | Out-Null
                Write-Host 'selftest: start OK'

                $st = Read-YghvState
                if ($st.active -ne 1 -or $st.pageCount -ne 1 -or $st.pid -ne $pidVal) {
                    throw ("selftest: state mismatch after start (active={0} pid={1} page_count={2})" -f
                        $st.active, $st.pid, $st.pageCount)
                }

                [Runtime.InteropServices.Marshal]::WriteInt64(
                    [IntPtr]$addr, 0x1122334455667788)
                $readback = [Runtime.InteropServices.Marshal]::ReadInt64([IntPtr]$addr)
                if ($readback -ne 0x1122334455667788) {
                    throw 'selftest: write/read mismatch on protected page'
                }
                Write-Host 'selftest: user write/read OK (page armed in NPT)'

                Invoke-YghvIoctl -Code ([YghvCtlNative]::IoCtl(0x804)) | Out-Null
                Invoke-YghvIoctl -Code ([YghvCtlNative]::IoCtl(0x802)) `
                    -InBytes ([BitConverter]::GetBytes([uint64]$addr)) | Out-Null

                $st = Read-YghvState
                if ($st.active -ne 0 -or $st.pageCount -ne 0) {
                    throw ("selftest: state mismatch after stop (active={0} page_count={1})" -f
                        $st.active, $st.pageCount)
                }
                Write-Host 'selftest: PASS'
            } finally {
                $gc.Free()
            }
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

                $st = Read-YghvState
                if ($st.pid -ne $childPid) {
                    throw ("exit-test: target not set (pid={0})" -f $st.pid)
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
                    $st = Read-YghvState
                } while (($st.active -ne 0 -or $st.pid -ne 0) -and
                    (Get-Date) -lt $deadline)
                if ($st.active -ne 0 -or $st.pid -ne 0) {
                    throw ("exit-test: auto disarm not observed (active={0} pid={1} page_count={2})" -f
                        $st.active, $st.pid, $st.pageCount)
                }
                Write-Host ("exit-test: PASS (active=0 pid=0 page_count={0})" -f
                    $st.pageCount)
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
