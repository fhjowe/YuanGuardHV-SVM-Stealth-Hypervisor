param(
    [string]$ServiceName = 'yuanguard'
)

$ErrorActionPreference = 'Stop'

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error 'This script requires an elevated PowerShell session.'
    exit 1
}

$source = @'
using System;
using System.Runtime.InteropServices;

public struct YghvUnicodeString
{
    public ushort Length;
    public ushort MaximumLength;
    public IntPtr Buffer;
}

public struct YghvLuid
{
    public uint LowPart;
    public int HighPart;
}

public struct YghvTokenPrivileges
{
    public uint PrivilegeCount;
    public YghvLuid Luid;
    public uint Attributes;
}

public static class YghvNtDriver
{
    [DllImport("ntdll.dll", CharSet = CharSet.Unicode)]
    public static extern void RtlInitUnicodeString(
        ref YghvUnicodeString target,
        [MarshalAs(UnmanagedType.LPWStr)] string source);

    [DllImport("ntdll.dll")]
    public static extern int NtUnloadDriver(ref YghvUnicodeString serviceName);
}

public static class YghvPrivilege
{
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
        out YghvLuid luid);

    [DllImport("advapi32.dll", SetLastError = true)]
    public static extern bool AdjustTokenPrivileges(
        IntPtr tokenHandle,
        bool disableAllPrivileges,
        ref YghvTokenPrivileges newState,
        uint bufferLength,
        IntPtr previousState,
        IntPtr returnLength);

    public static bool EnableLoadDriverPrivilege()
    {
        IntPtr token;
        if (!OpenProcessToken(GetCurrentProcess(), 0x28, out token))
            return false;
        try
        {
            YghvLuid luid;
            if (!LookupPrivilegeValue(null, "SeLoadDriverPrivilege", out luid))
                return false;

            YghvTokenPrivileges tp = new YghvTokenPrivileges();
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

if (-not ('YghvPrivilege' -as [type])) {
    Add-Type -TypeDefinition $source
}

if (-not [YghvPrivilege]::EnableLoadDriverPrivilege()) {
    Write-Error 'Unable to enable SeLoadDriverPrivilege. Run this script from an elevated PowerShell.'
    exit 1
}

$registryPath = '\Registry\Machine\System\CurrentControlSet\Services\' + $ServiceName
$unicode = [YghvUnicodeString]::new()
[YghvNtDriver]::RtlInitUnicodeString([ref]$unicode, $registryPath)
$status = [YghvNtDriver]::NtUnloadDriver([ref]$unicode)
$statusUint = [BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$status), 0)

if ($status -eq 0) {
    Write-Host "Unloaded service '$ServiceName'."
} else {
    Write-Host ("NtUnloadDriver failed for '{0}': 0x{1:X8}" -f $ServiceName, $statusUint)
}

& sc.exe query $ServiceName
exit 0
