@echo off
"C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\kd.exe" -k com:pipe,port=\\.\pipe\yuanhv_debug,resets=0 -c ".reload;bu yuanguard!DriverEntry;g" -loga D:\yuanguard\YuanGuardHV\kd_auto.log
