$ErrorActionPreference = 'Stop'

$scripts = @(
    'ioctl_parity.ps1',
    'command_parity.ps1',
    'safety_checks.ps1'
)

foreach ($script in $scripts) {
    Write-Host "[RUN] $script"
    & (Join-Path $PSScriptRoot $script)
}

Write-Host 'ALL STATIC CHECKS PASS'
