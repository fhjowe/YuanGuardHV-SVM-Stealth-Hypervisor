@echo off
setlocal
cd /d "%~dp0"

if "%JAVA_HOME%"=="" set "JAVA_HOME=D:\DevTools\zulu21"
"%JAVA_HOME%\bin\java" -Djava.library.path=native -cp . YghvCtl %*
exit /b %errorlevel%
