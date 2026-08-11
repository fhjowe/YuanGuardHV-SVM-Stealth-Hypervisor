@echo off
setlocal
cd /d "%~dp0"

if "%JAVA_HOME%"=="" set "JAVA_HOME=D:\DevTools\zulu21"
if not exist "%JAVA_HOME%\include\jni.h" (
    echo [ERROR] jni.h not found under JAVA_HOME=%JAVA_HOME%
    exit /b 1
)

set "CLANG=C:\Program Files\LLVM\bin\clang-cl.exe"
if not exist "%CLANG%" (
    echo [ERROR] clang-cl not found
    exit /b 1
)

"%CLANG%" /nologo /O2 /LD /D_JNI_IMPLEMENTATION_ ^
    /I"%JAVA_HOME%\include" /I"%JAVA_HOME%\include\win32" ^
    native\yghv_ctl_jni.c /Fe:native\yghv_ctl_jni.dll /link kernel32.lib
if errorlevel 1 exit /b 1

echo [YuanGuardHV] JNI client built: %CD%\native\yghv_ctl_jni.dll
exit /b 0
