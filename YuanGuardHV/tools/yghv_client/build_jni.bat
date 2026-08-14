@echo off
setlocal
cd /d "%~dp0"

rem REV-034: make toolchain paths overridable via env vars (defaults preserved).
if "%YGHV_JAVA_HOME%"=="" set "YGHV_JAVA_HOME=%JAVA_HOME%"
if "%YGHV_JAVA_HOME%"=="" set "YGHV_JAVA_HOME=D:\DevTools\zulu21"
if not exist "%YGHV_JAVA_HOME%\include\jni.h" (
    echo [ERROR] jni.h not found under YGHV_JAVA_HOME=%YGHV_JAVA_HOME%
    exit /b 1
)

if "%YGHV_CLANG%"=="" set "YGHV_CLANG=C:\Program Files\LLVM\bin\clang-cl.exe"
if not exist "%YGHV_CLANG%" (
    echo [ERROR] clang-cl not found at %YGHV_CLANG%
    exit /b 1
)

"%YGHV_CLANG%" /nologo /O2 /LD /D_JNI_IMPLEMENTATION_ ^
    /I"%YGHV_JAVA_HOME%\include" /I"%YGHV_JAVA_HOME%\include\win32" ^
    native\yghv_ctl_jni.c /Fe:native\yghv_ctl_jni.dll /link kernel32.lib advapi32.lib
if errorlevel 1 exit /b 1

echo [YuanGuardHV] JNI client built: %CD%\native\yghv_ctl_jni.dll
exit /b 0
