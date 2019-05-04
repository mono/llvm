:: Set up build environment and build LLVM using build-external-llvm.bat.

:: Arguments:
:: -------------------------------------------------------
:: -------------------------------------------------------

@echo off
setlocal

set BUILD_RESULT=1

:: Get path for current running script.
set RUN_JENKINS_WINDOWS_SCRIPT_PATH=%~dp0
set MONO_LLVM_SRC_DIR=%RUN_JENKINS_WINDOWS_SCRIPT_PATH%..\..
set MONO_LLVM_BUILD_DIR=%RUN_JENKINS_WINDOWS_SCRIPT_PATH%builds\llvm-llvmwin64-msvc
set MONO_LLVM_INSTALL_DIR=%RUN_JENKINS_WINDOWS_SCRIPT_PATH%out\llvm-llvmwin64-msvc

if exist "%MONO_LLVM_BUILD_DIR%" (
    rmdir /S /Q "%MONO_LLVM_BUILD_DIR%"
)

if exist "%MONO_LLVM_INSTALL_DIR%" (
    rmdir /S /Q "%MONO_LLVM_INSTALL_DIR%"
)

mkdir "%MONO_LLVM_BUILD_DIR%"
mkdir "%MONO_LLVM_INSTALL_DIR%"

:: Setup Windows environment.
call %RUN_JENKINS_WINDOWS_SCRIPT_PATH%setup-windows-env.bat

:: Setup VS msvc build environment.
call %RUN_JENKINS_WINDOWS_SCRIPT_PATH%setup-vs-msvcbuild-env.bat

:: Run LLVM builds using default argument (similar to how its build from mono.sln)
call %RUN_JENKINS_WINDOWS_SCRIPT_PATH%build-external-llvm.bat "%MONO_LLVM_SRC_DIR%" "%MONO_LLVM_BUILD_DIR%" "%MONO_LLVM_INSTALL_DIR%" "%MONO_LLVM_BUILD_DIR%" "-D__default_codegen__ -D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE -DHAVE_CONFIG_H -DGC_NOT_DLL -DWIN32_THREADS -DWINVER=0x0601 -D_WIN32_WINNT=0x0601 -D_WIN32_IE=0x0501 -D_UNICODE -DUNICODE -DFD_SETSIZE=1024 -DNVALGRIND" "-DLLVM_ENABLE_ASSERTIONS=Off" "x64" "Release" "Build" "" "false" && (
    set BUILD_RESULT=0
) || (
    set BUILD_RESULT=1
    if not %ERRORLEVEL% == 0 (
        set BUILD_RESULT=%ERRORLEVEL%
    )
)

exit /b %BUILD_RESULT%

@echo on