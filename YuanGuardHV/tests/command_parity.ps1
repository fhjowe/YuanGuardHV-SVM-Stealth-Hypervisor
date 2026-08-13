$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$psClient = Join-Path $root 'tools\yghv_ctl.ps1'
$javaClient = Join-Path $root 'tools\yghv_client\YghvCtl.java'
$readme = Join-Path $root 'tools\yghv_client\README.md'

function Get-PSCommands {
    $text = Get-Content -LiteralPath $psClient -Raw
    $set = @{}
    foreach ($m in [regex]::Matches($text, '(?m)^\s*''([a-z0-9-]+)''\s*\{\s*$')) {
        $set[$m.Groups[1].Value.ToLower()] = $true
    }
    return @($set.Keys | Sort-Object)
}

function Get-JavaCommands {
    $text = Get-Content -LiteralPath $javaClient -Raw
    $set = @{}
    foreach ($m in [regex]::Matches($text, 'case "([a-z0-9-]+)"')) {
        $set[$m.Groups[1].Value.ToLower()] = $true
    }
    return @($set.Keys | Sort-Object)
}

function Get-ReadmeJavaCommands {
    $text = Get-Content -LiteralPath $readme -Raw
    $set = @{}
    foreach ($m in [regex]::Matches($text, '(?m)^run\.bat\s+([a-z0-9-]+)')) {
        $set[$m.Groups[1].Value.ToLower()] = $true
    }
    return @($set.Keys | Sort-Object)
}

function Get-ReadmePSCommands {
    $text = Get-Content -LiteralPath $readme -Raw
    $set = @{}
    foreach ($m in [regex]::Matches($text, '(?m)^yghv_ctl\.ps1\s+([a-z0-9-]+)')) {
        $set[$m.Groups[1].Value.ToLower()] = $true
    }
    return @($set.Keys | Sort-Object)
}

$ps = Get-PSCommands
$java = Get-JavaCommands
$readmeJava = Get-ReadmeJavaCommands
$readmePS = Get-ReadmePSCommands

if (Compare-Object $java $readmeJava) {
    throw "Command parity FAIL: Java client vs README mismatch`nJava: $($java -join ',')`nREADME: $($readmeJava -join ',')"
}
if (Compare-Object $ps $readmePS) {
    throw "Command parity FAIL: PowerShell client vs README mismatch`nPS: $($ps -join ',')`nREADME: $($readmePS -join ',')"
}

Write-Host "[PASS] Command parity: PowerShell=$($ps.Count) Java=$($java.Count)"
