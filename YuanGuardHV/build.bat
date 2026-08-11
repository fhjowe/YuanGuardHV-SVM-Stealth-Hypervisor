@echo off
setlocal enabledelayedexpansion

set "PROJECT_DIR=%~dp0"
set "HV_DIR=%PROJECT_DIR%hv"
set "BIN_DIR=%PROJECT_DIR%bin"
set "WDK_ROOT=C:\Program Files (x86)\Windows Kits\10"

if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"

echo [YuanGuardHV] Building AMD-V Hypervisor Driver...

rem Find LLVM
set "LLVM_DIR=C:\Program Files\LLVM\bin"
if exist "%LLVM_DIR%\clang-cl.exe" (
    set "CLANG_CL=%LLVM_DIR%\clang-cl.exe"
) else (
    for %%i in (clang-cl.exe) do set "CLANG_CL=%%~$PATH:i"
)
if "%CLANG_CL%"=="" (
    echo [ERROR] clang-cl not found
    exit /b 1
)
echo   clang-cl: %CLANG_CL%

rem Find MSVC link.exe
set "MSVC_LINK=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\link.exe"
if exist "%MSVC_LINK%" goto :have_link
echo [ERROR] MSVC link.exe not found at "%MSVC_LINK%"
exit /b 1
:have_link
echo   link.exe: %MSVC_LINK%

rem Find WDK
if exist "%WDK_ROOT%" goto :have_wdk
echo [ERROR] WDK not found at %WDK_ROOT%
exit /b 1
:have_wdk

set "WDK_VER=10.0.19041.0"

set "INCLUDES=/I"%WDK_ROOT%\Include\%WDK_VER%\km""
set "INCLUDES=%INCLUDES% /I"%WDK_ROOT%\Include\%WDK_VER%\shared""
set "INCLUDES=%INCLUDES% /I"%WDK_ROOT%\Include\%WDK_VER%\um""
set "INCLUDES=%INCLUDES% /I"%HV_DIR%\common""
set "INCLUDES=%INCLUDES% /I"%HV_DIR%\pool""

set "CFLAGS=/nologo /O2 /kernel /GR- /EHs-c- /Zl /GS-"
set "CFLAGS=%CFLAGS% -Wno-microsoft -Wno-unknown-pragmas -Wno-ignored-attributes -Wno-visibility -Wno-pragma-pack"
set "CFLAGS=%CFLAGS% /D_KERNEL_MODE /D_AMD64_ /DNTDDI_VERSION=0x0A000005 /DYGHV_DEBUG_LOG"

set "LINKS=/nologo /SUBSYSTEM:NATIVE /DRIVER:WDM /ENTRY:DriverEntry /MACHINE:X64"
set "LINKS=%LINKS% /OPT:NOREF,NOICF"
set "LINKS=%LINKS% /LIBPATH:"%WDK_ROOT%\Lib\%WDK_VER%\km\x64""

echo   WDK Version: %WDK_VER%
echo   Compiling...

for %%f in (main svm_core npt_core vmexit vmmcall multi_core protect control_device) do call :compile %%f || goto :error

echo   svm_trampoline.S
"%CLANG_CL%" %CFLAGS% %INCLUDES% /c /Fo"%BIN_DIR%\svm_trampoline.obj" "%HV_DIR%\svm_trampoline.S"
if errorlevel 1 goto :error

echo   Linking (MSVC)...
"%MSVC_LINK%" %LINKS% /OUT:"%BIN_DIR%\yuanguard_hv.sys" ^
    "%BIN_DIR%\main.obj" ^
    "%BIN_DIR%\svm_core.obj" ^
    "%BIN_DIR%\npt_core.obj" ^
    "%BIN_DIR%\vmexit.obj" ^
    "%BIN_DIR%\vmmcall.obj" ^
    "%BIN_DIR%\multi_core.obj" ^
    "%BIN_DIR%\protect.obj" ^
    "%BIN_DIR%\control_device.obj" ^
    "%BIN_DIR%\svm_trampoline.obj" ^
    ntoskrnl.lib
if errorlevel 1 goto :error

echo [YuanGuardHV] Build SUCCESS: %BIN_DIR%\yuanguard_hv.sys
echo   signing...
"C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe" sign /fd SHA256 /a /f "%PROJECT_DIR%yuanguard_test.cer" "%BIN_DIR%\yuanguard_hv.sys"
certutil -hashfile "%BIN_DIR%\yuanguard_hv.sys" SHA256
exit /b 0

:compile
echo   %1.c
"%CLANG_CL%" %CFLAGS% %INCLUDES% /c /Fo"%BIN_DIR%\%1.obj" "%HV_DIR%\%1.c"
if errorlevel 1 exit /b 1
exit /b 0

:error
echo [ERROR] Build FAILED
exit /b 1
