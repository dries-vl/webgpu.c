@echo off

REM Set up Visual Studio environment if not already set
if not defined VSINSTALLDIR (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
)

REM Change to the script's directory to make sure we have the right context
pushd "%~dp0"

REM Ensure the "debug" folder exists
if not exist "debug" mkdir "debug"
cd debug

cl -FC -Zi -Ox -GL -Gw -GS- -fp:fast -favor:INTEL64 "..\main.c" "..\wgpu_native.dll.lib" user32.lib

if %ERRORLEVEL% neq 0 (
    echo Compilation failed.
    popd
    exit /b %ERRORLEVEL%
)

xcopy /E /I /Y /Q "..\data" "data"
copy /Y "..\wgpu_native.dll" "wgpu_native.dll"

raddbg --auto_step -q main.exe

popd
