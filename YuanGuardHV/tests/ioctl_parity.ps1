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

# REV-032: also verify the full CTL_CODE constants (device type / method /
# access) are identical across the header and all three client implementations,
# so a drift in the formula itself is caught, not just the function numbers.
$jniClient = Join-Path (Split-Path -Parent $javaClient) 'native\yghv_ctl_jni.c'

function Get-CConstant {
    param([string]$Pattern, [string]$Path)
    $text = Get-Content -LiteralPath $Path -Raw
    if ($text -match $Pattern) { return [Convert]::ToInt64($Matches[1], 16) }
    return -1
}

$cDevType  = Get-CConstant -Path $header    -Pattern '#define\s+YGHV_IOCTL_DEVICE_TYPE\s+(0x[0-9A-Fa-f]+)'
$cMethod   = Get-CConstant -Path $header    -Pattern '#define\s+YGHV_METHOD_BUFFERED\s+(\d+)'
$cAccess   = Get-CConstant -Path $header    -Pattern '#define\s+YGHV_FILE_ANY_ACCESS\s+(\d+)'
$psDevType = Get-CConstant -Path $psClient  -Pattern '\(0x([0-9A-Fa-f]+)u?\s*<<\s*16\)'
$javaDev   = Get-CConstant -Path $javaClient -Pattern '\(0x([0-9A-Fa-f]+)\s*<<\s*16\)'
$jniDev    = Get-CConstant -Path $jniClient -Pattern '0x([0-9A-Fa-f]+)\s*<<\s*16'

if ($cDevType -ne 0x5947) { throw "IOCTL parity FAIL: header device type 0x$($cDevType.ToString('X')) != 0x5947" }
if ($cMethod -ne 0 -or $cAccess -ne 0) {
    throw "IOCTL parity FAIL: header METHOD=$cMethod ACCESS=$cAccess (expected 0/0)"
}
if ($psDevType -ne $cDevType -or $javaDev -ne $cDevType -or $jniDev -ne $cDevType) {
    throw "IOCTL parity FAIL: device-type drift C=0x$($cDevType.ToString('X')) PS=0x$($psDevType.ToString('X')) Java=0x$($javaDev.ToString('X')) JNI=0x$($jniDev.ToString('X'))"
}

Write-Host "[PASS] IOCTL parity: C=$($c.Count) PowerShell=$($ps.Count) Java=$($java.Count)"
Write-Host "[PASS] IOCTL constants: device type=0x5947 METHOD=0 ACCESS=0 across C/PS/Java/JNI"
