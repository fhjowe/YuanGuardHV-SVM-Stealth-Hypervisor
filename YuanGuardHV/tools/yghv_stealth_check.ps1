$ErrorActionPreference = 'Continue'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class YghvStealthNative
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

    [DllImport("kernel32.dll")]
    public static extern bool CloseHandle(IntPtr hObject);
}
'@

$visible = 0

function Report {
    param([string]$Name, [bool]$Found, [string]$Detail)
    if ($Found) {
        $script:visible++
        Write-Host ("[VISIBLE] {0} : {1}" -f $Name, $Detail)
    } else {
        Write-Host ("[CLEAN]   {0}" -f $Name)
    }
}

$service = Get-CimInstance Win32_Service -Filter "Name='yuanguard'" `
    -ErrorAction SilentlyContinue
Report -Name 'service yuanguard' -Found ($null -ne $service) `
    -Detail ("State={0} Start={1} Path={2}" -f $service.State, $service.StartMode,
        $service.PathName)

$driver = Get-CimInstance Win32_SystemDriver -Filter "Name='yuanguard'" `
    -ErrorAction SilentlyContinue
Report -Name 'kernel driver yuanguard' -Found ($null -ne $driver) `
    -Detail ("State={0} Path={1}" -f $driver.State, $driver.PathName)

$regPath = 'HKLM:\SYSTEM\CurrentControlSet\Services\yuanguard'
Report -Name 'registry service key' -Found (Test-Path -LiteralPath $regPath) `
    -Detail $regPath

$files = @(
    'C:\yuanguard_hv.sys',
    'C:\Windows\yghv_progress.log',
    'C:\Windows\yghv_ioctl.log',
    'C:\Windows\yghv_hook.log'
)
foreach ($file in $files) {
    Report -Name ("file {0}" -f $file) -Found (Test-Path -LiteralPath $file) `
        -Detail $file
}

$h = [YghvStealthNative]::CreateFile(
    '\\.\YuanGuardHV', 0, 3, [IntPtr]::Zero, 3, 0, [IntPtr]::Zero)
if ($h -ne [IntPtr]::Zero -and $h -ne [IntPtr]::new(-1)) {
    [YghvStealthNative]::CloseHandle($h) | Out-Null
    Report -Name 'control device \\.\YuanGuardHV' -Found $true `
        -Detail 'CreateFile opened successfully'
} else {
    Report -Name 'control device \\.\YuanGuardHV' -Found $false -Detail ''
}

Write-Host ("Stealth check complete: visible traces = {0}" -f $visible)
