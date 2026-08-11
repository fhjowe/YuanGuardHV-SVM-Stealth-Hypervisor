@echo off
setlocal
cd /d "%~dp0"

call build_jni.bat || exit /b 1

if "%JAVA_HOME%"=="" set "JAVA_HOME=D:\DevTools\zulu21"
"%JAVA_HOME%\bin\javac" YghvCtl.java test\Sleepy.java || exit /b 1

echo [YuanGuardHV] Java client compiled.
exit /b 0
