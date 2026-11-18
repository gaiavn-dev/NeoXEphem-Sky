@echo off
rem Build NeoXEphem Sky with MSVC + CMake + Ninja.
rem Run from any prompt; it finds Visual Studio 2019/2022 via vswhere.
setlocal
cd /d "%~dp0"
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist %VSWHERE% (echo vswhere.exe not found - install Visual Studio Build Tools & exit /b 1)
for /f "usebackq delims=" %%i in (`%VSWHERE% -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if not defined VSDIR (echo No Visual Studio with C++ tools found & exit /b 1)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build %* || exit /b 1
echo.
echo Built: build\NeoXEphemSky.exe  build\neox-sky.exe  build\astrotest.exe
