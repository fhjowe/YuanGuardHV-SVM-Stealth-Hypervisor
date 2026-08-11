$ErrorActionPreference = 'Stop'

$kdBin   = 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\kd.exe'
$cmdFile = 'D:\yuanguard\YuanGuardHV\kd_cmd.txt'
$logFile = 'D:\yuanguard\YuanGuardHV\kd_ctl.log'
$inLog   = 'D:\yuanguard\YuanGuardHV\kd_ctl_in.txt'
$outLog  = 'D:\yuanguard\YuanGuardHV\kd_ctl_out.txt'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class KdCtlConsole
{
    [DllImport("kernel32.dll")]
    public static extern bool SetConsoleCtrlHandler(IntPtr handler, bool add);
}
'@
[KdCtlConsole]::SetConsoleCtrlHandler([IntPtr]::Zero, $true) | Out-Null

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $kdBin
$psi.Arguments = '-b -k com:pipe,port=\\.\pipe\yuanhv_debug,resets=0 -logo "' + $logFile + '"'
$psi.UseShellExecute = $false
$psi.RedirectStandardInput  = $true
$psi.RedirectStandardOutput = $false
$psi.RedirectStandardError  = $false

$p = [System.Diagnostics.Process]::Start($psi)

try {
    while (-not $p.HasExited) {
        Start-Sleep -Milliseconds 400
        if (Test-Path -LiteralPath $cmdFile) {
            $lines = @(Get-Content -LiteralPath $cmdFile)
            foreach ($line in $lines) {
                $cmd = $line.Trim()
                if ($cmd -eq '') { continue }
                if ($cmd -eq 'quit') {
                    $p.StandardInput.WriteLine('q')
                    Start-Sleep -Seconds 1
                    if (-not $p.HasExited) { $p.Kill() }
                    exit 0
                }
                if ($cmd -eq '__ctrl_c__') {
                    $p.StandardInput.Write([char]3)
                    Set-Content -LiteralPath $cmdFile -Value ''
                    continue
                }
                Add-Content -LiteralPath $inLog -Value $cmd
                $p.StandardInput.WriteLine($cmd)
            }
            Set-Content -LiteralPath $cmdFile -Value ''
        }
    }
} catch {
    Add-Content -LiteralPath 'D:\yuanguard\YuanGuardHV\kd_ctl_err.txt' -Value ("ERR: " + $_.Exception.Message)
    if (-not $p.HasExited) { $p.Kill() }
    exit 1
}
