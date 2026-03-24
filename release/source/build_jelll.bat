@echo off
setlocal enabledelayedexpansion
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

where cl >nul 2>nul
if %ERRORLEVEL%==0 goto BUILD_MSVC

if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VSINSTALL=%%I"
  )
)
if defined VSINSTALL (
  if exist "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" (
    call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
    where cl >nul 2>nul
    if !ERRORLEVEL!==0 goto BUILD_MSVC
  )
)

where g++ >nul 2>nul
if %ERRORLEVEL%==0 goto BUILD_GPP

if exist "C:\msys64\mingw64\bin\g++.exe" (
  set "PATH=C:\msys64\mingw64\bin;%PATH%"
  where g++ >nul 2>nul
  if !ERRORLEVEL!==0 goto BUILD_GPP
)
if exist "C:\mingw64\bin\g++.exe" (
  set "PATH=C:\mingw64\bin;%PATH%"
  where g++ >nul 2>nul
  if !ERRORLEVEL!==0 goto BUILD_GPP
)

echo C++20 compiler not found. Install MSVC Build Tools or MinGW g++.
exit /b 1

:BUILD_MSVC
  echo [JELLL] Building with MSVC...
  cl /std:c++20 /EHsc /O2 /Fe:jelll.exe jelll.cpp
if %ERRORLEVEL% neq 0 exit /b 1
goto BUILD_OK

:BUILD_GPP
echo [JELLL] Building with MinGW g++...
g++ -std=c++20 -O2 -o jelll.exe jelll.cpp
if %ERRORLEVEL%==0 goto BUILD_OK
echo [JELLL] Retrying with -lstdc++fs...
g++ -std=c++20 -O2 -o jelll.exe jelll.cpp -lstdc++fs
if %ERRORLEVEL% neq 0 exit /b 1

:BUILD_OK
echo jelll.exe build succeeded
exit /b 0
