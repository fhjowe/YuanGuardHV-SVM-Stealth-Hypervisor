$kdBin = "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\kd.exe"
$kdArgs = @('-k', 'com:pipe,port=\\.\pipe\yuanhv_debug,baud=115200',
            '-c', '.sympath srv*;.reload;bu yuanguard!DriverEntry;g',
            '-loga', 'D:\yuanguard\YuanGuardHV\kd_output.log')

$proc = Start-Process -FilePath $kdBin -ArgumentList $kdArgs -PassThru -NoNewWindow -RedirectStandardInput $null
Write-Host "KD PID: $($proc.Id)"
Write-Host "Waiting for connection..."
Start-Sleep 45
Write-Host "--- KD LOG ---"
Get-Content 'D:\yuanguard\YuanGuardHV\kd_output.log' -Tail 20
