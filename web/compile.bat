@echo off
REM Set up Emscripten environment
where emcc >nul 2>nul
if errorlevel 1 (
    echo emcc not found in PATH, setting up Emscripten...
    call %USERPROFILE%\dev\emsdk\emsdk activate latest
    call %USERPROFILE%\dev\emsdk\emsdk_env.bat
) else (
    echo emcc already available, skipping Emscripten setup
)

call emcc main.c -o index.html -sUSE_WEBGPU=1 -sALLOW_MEMORY_GROWTH -sEXPORTED_FUNCTIONS=_main -sEXPORTED_RUNTIME_METHODS=ccall,cwrap

if errorlevel 1 (
    echo Compile failed.
    exit /b
) else (
    echo Compile succeeded
)

REM timeout /t 1 >nul
REM start chrome --new-tab http://localhost:8000
REM python -m http.server 8000
