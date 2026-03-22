@echo off
setlocal enabledelayedexpansion

echo ============================================
echo   JEBC - Standalone Setup
echo ============================================
echo.

echo [JEBC] Checking esbuild...
call npm list -g esbuild >nul 2>nul
if !ERRORLEVEL! neq 0 (
    echo [JEBC] Installing esbuild globally...
    call npm install -g esbuild
) else (
    echo [OK] esbuild already installed.
)

echo [JEBC] Checking Emscripten...
where emcc >nul 2>nul
if !ERRORLEVEL! neq 0 (
    where git >nul 2>nul
    if !ERRORLEVEL! neq 0 (
        echo [ERROR] git is not installed. https://git-scm.com/
        exit /b 1
    )
    if not exist "emsdk" (
        echo [SETUP] Cloning emsdk...
        git clone https://github.com/emscripten-core/emsdk.git emsdk
    )
    pushd emsdk
    call emsdk install latest
    call emsdk activate latest
    call emsdk_env.bat
    popd
    echo [OK] Emscripten installed.
) else (
    echo [OK] emcc already available.
)

echo.
echo   Done! You can now use: jebc your_app.jebc
echo.

endlocal
