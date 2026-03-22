@echo off
setlocal enabledelayedexpansion
echo [JEBC] Emscripten Setup
where emcc >nul 2>nul
if !ERRORLEVEL! neq 0 (
    where git >nul 2>nul
    if !ERRORLEVEL! neq 0 ( echo [ERROR] git required. & exit /b 1 )
    if not exist "emsdk" ( git clone https://github.com/emscripten-core/emsdk.git emsdk )
    pushd emsdk & call emsdk install latest & call emsdk activate latest & call emsdk_env.bat & popd
) else ( echo [OK] emcc available. )
echo Done!
endlocal
