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

call emcc ..\webgpu.c main.c -o index.html ^
  -sUSE_WEBGPU=1 ^
  -sUSE_PTHREADS=1 ^
  -sPTHREAD_POOL_SIZE=1 ^
  -sPROXY_TO_PTHREAD=1 ^
  -sOFFSCREENCANVAS_SUPPORT=1 ^
  -sALLOW_MEMORY_GROWTH=1 ^
  -sEXPORTED_FUNCTIONS=_main ^
  -sEXPORTED_RUNTIME_METHODS=ccall,cwrap ^
  --preload-file "../data/textures/bin@data/textures/bin" ^
  --preload-file "../data/models/blender/bin@data/models/blender/bin" ^
  --preload-file "../data/models/bin@data/models/bin" ^
  --preload-file "../data/shaders@data/shaders" ^
  -gsource-map

if errorlevel 1 (
    echo Compile failed.
    exit /b
) else (
    echo Compile succeeded
)

REM timeout /t 1 >nul
REM start chrome --new-tab http://localhost:8000
REM python -m http.server 8000
