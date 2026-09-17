@echo off
rem ============================================================================
rem build_simplesvm.bat - C0 control-experiment build for upstream SimpleSvm
rem (tandasat/SimpleSvm, master) on THIS machine's toolchain, bypassing the
rem WDK MSBuild integration (mirrors YuanGuardHV\build.bat style: absolute
rem cl.exe/ml64.exe/link.exe + signtool with the repo test certificate).
rem
rem Source tree:  <repo>\thirdparty\SimpleSvm   (clone method in docs)
rem Output:       <repo>\thirdparty\SimpleSvm\bin\SimpleSvm.sys (signed)
rem NOTE: output dirs are covered by the root .gitignore (thirdparty/, *.obj,
rem       *.sys) - nothing here enters git.
rem ============================================================================
setlocal enabledelayedexpansion

set "PROJECT_DIR=%~dp0"
for %%a in ("%PROJECT_DIR%..\..") do set "REPO_DIR=%%~fa"
set "SS_DIR=%REPO_DIR%\thirdparty\SimpleSvm\SimpleSvm"
set "BIN_DIR=%REPO_DIR%\thirdparty\SimpleSvm\bin"

set "WDK_ROOT=C:\Program Files (x86)\Windows Kits\10"
set "WDK_VER=10.0.26100.0"
set "MSVC_DIR=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207"
set "CLX=%MSVC_DIR%\bin\Hostx64\x64\cl.exe"
set "ML64=%MSVC_DIR%\bin\Hostx64\x64\ml64.exe"
set "LINKX=%MSVC_DIR%\bin\Hostx64\x64\link.exe"
set "SIGNTOOL=%WDK_ROOT%\bin\10.0.26100.0\x64\signtool.exe"

if not exist "%SS_DIR%\SimpleSvm.cpp" goto :no_src
for %%t in ("%CLX%" "%ML64%" "%LINKX%" "%SIGNTOOL%") do if not exist %%t echo [WARN] missing tool: %%t

if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"

set "INCLUDE=%WDK_ROOT%\Include\%WDK_VER%\km\crt;%WDK_ROOT%\Include\%WDK_VER%\km;%WDK_ROOT%\Include\%WDK_VER%\shared;%WDK_ROOT%\Include\%WDK_VER%\shared\win32;%WDK_ROOT%\Include\%WDK_VER%\ucrt;%MSVC_DIR%\include"
set "LIB=%WDK_ROOT%\Lib\%WDK_VER%\km\x64;%MSVC_DIR%\lib\x64"
set "DEFS=/D_AMD64_ /DNDEBUG /DNTDDI_VERSION=0x0A000005 /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00"
set "CXXFLAGS=/nologo /c /kernel /O2 /GS /GR- /EHs-c- /Zl /std:c++17 /W3 /utf-8"

echo   Assembling x64.asm...
"%ML64%" /nologo /c /Fo"%BIN_DIR%\x64.obj" "%SS_DIR%\x64.asm"
if errorlevel 1 goto :error

echo   Compiling SimpleSvm.cpp...
"%CLX%" %CXXFLAGS% %DEFS% /Fo"%BIN_DIR%\SimpleSvm.obj" "%SS_DIR%\SimpleSvm.cpp"
if errorlevel 1 goto :error

echo   Linking...
"%LINKX%" /nologo /SUBSYSTEM:NATIVE /DRIVER:WDM /ENTRY:DriverEntry /MACHINE:X64 ^
    /NODEFAULTLIB /OPT:NOREF,NOICF ^
    /OUT:"%BIN_DIR%\SimpleSvm.sys" ^
    "%BIN_DIR%\SimpleSvm.obj" "%BIN_DIR%\x64.obj" ^
    ntoskrnl.lib hal.lib BufferOverflowK.lib libcntpr.lib
if errorlevel 1 goto :error

echo [build_simplesvm] Build SUCCESS: %BIN_DIR%\SimpleSvm.sys
echo   signing (test cert, same as YuanGuardHV build.bat)...
"%SIGNTOOL%" sign /fd SHA256 /a /f "%REPO_DIR%\YuanGuardHV\yuanguard_test.cer" "%BIN_DIR%\SimpleSvm.sys" || goto :error
certutil -hashfile "%BIN_DIR%\SimpleSvm.sys" SHA256
exit /b 0

:error
echo [ERROR] build_simplesvm FAILED
exit /b 1

:no_src
echo [ERROR] upstream source missing: %SS_DIR%\SimpleSvm.cpp
echo         Clone it first. See docs\YGHV_SIMPLEVM_LEVERAGE_20260918.md - section C0
echo         includes the codeload tarball command for restricted networks.
exit /b 1
