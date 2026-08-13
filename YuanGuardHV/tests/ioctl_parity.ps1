$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$header = Join-Path $root 'hv\common\control_ioctl.h'
$psClient = Join-Path $root 'tools\yghv_ctl.ps1'
$javaClient = Join-Path $root 'tools\yghv_client\YghvCtl.java'

function Get-CIoctlFunctions {
    param([string]$Path)
    $text = Get-Content -LiteralPath $Path -Raw
    $map = @{}
    foreach ($m in [regex]::Matches(
        $text,
        '#define\s+(IOCTL_YGHV_\w+)\s+\\?\s*YGHV_CTL_CODE\([^,]+,\s*(0x[0-9A-Fa-f]+)')) {
        $map[$m.Groups[1].Value] = [Convert]::ToInt64($m.Groups[2].Value, 16)
    }
    return $map
}

function Get-PSFunctions {
    $text = Get-Content -LiteralPath $psClient -Raw
    $set = @{}
    foreach ($m in [regex]::Matches($text, 'IoCtl\((0x[0-9A-Fa-f]+)\)')) {
        $set[[Convert]::ToInt64($m.Groups[1].Value, 16)] = $true
    }
    return @($set.Keys | Sort-Object)
}

function Get-JavaFunctions {
    $text = Get-Content -LiteralPath $javaClient -Raw
    $set = @{}
    foreach ($m in [regex]::Matches($text, 'FN_\w+\s*=\s*(0x[0-9A-Fa-f]+)')) {
        $set[[Convert]::ToInt64($m.Groups[1].Value, 16)] = $true
    }
    return @($set.Keys | Sort-Object)
}

$c = Get-CIoctlFunctions -Path $header
$ps = Get-PSFunctions
$java = Get-JavaFunctions
$cValues = @($c.Values | Sort-Object)

if (Compare-Object $cValues $ps) {
    throw "IOCTL parity FAIL: C vs PowerShell mismatch`nC: $($cValues -join ',')`nPS: $($ps -join ',')"
}
if (Compare-Object $cValues $java) {
    throw "IOCTL parity FAIL: C vs Java mismatch`nC: $($cValues -join ',')`nJava: $($java -join ',')"
}

Write-Host "[PASS] IOCTL parity: C=$($c.Count) PowerShell=$($ps.Count) Java=$($java.Count)"
