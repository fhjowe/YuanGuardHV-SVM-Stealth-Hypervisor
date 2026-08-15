@echo off
REM 9.207: removed `bu yuanguard!DriverEntry` breakpoint — it froze the guest at
REM DriverEntry whenever kd was truly attached (the SCM saw "RUNNING" but the
REM guest stalled waiting for kd input). Now just .reload;g (no pause).
"C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\kd.exe" -k com:pipe,port=\\.\pipe\yuanhv_debug,resets=0 -c ".reload;g" -loga D:\yuanguard\YuanGuardHV\kd_auto.log
