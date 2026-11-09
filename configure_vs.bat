@echo off
setlocal EnableExtensions
cd /d "%~dp0"

where cmake >nul 2>&1
if errorlevel 1 (
  echo cmake was not found on PATH.
  exit /b 1
)

set "PTLIB_DIR=%~1"
if not "%PTLIB_DIR%"=="" goto have_ptlib
set "PTLIB_DIR=%~dp0..\ptlib\build"
if exist "%PTLIB_DIR%\PTLibConfig.cmake" goto have_ptlib
echo PTLib was not found at %PTLIB_DIR%
echo Build PTLib first, or pass its build directory or install prefix:
echo   configure_vs.bat C:\ptlib\build
exit /b 1

:have_ptlib
call :generator

set "TOOLCHAIN="
if defined VCPKG_ROOT if exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
  set "TOOLCHAIN=-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
)
if not defined TOOLCHAIN echo VCPKG_ROOT is not set. CMake will look for vcpkg.

echo Configuring OPAL with "%GENERATOR%"
echo PTLib: %PTLIB_DIR%
cmake -S . -B build -G "%GENERATOR%" -A x64 -DOPAL_PTLIB_DIR="%PTLIB_DIR%" %TOOLCHAIN%
if errorlevel 1 exit /b 1

echo.
echo Created %CD%\build\OPAL.sln
exit /b 0

:generator
set "GENERATOR=Visual Studio 17 2022"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 0
set "VS_MAJOR="
set "VS_YEAR="
for /f "usebackq tokens=1 delims=." %%V in (`"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion`) do set "VS_MAJOR=%%V"
for /f "usebackq delims=" %%Y in (`"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property catalog_productLineVersion`) do set "VS_YEAR=%%Y"
if not defined VS_MAJOR exit /b 0
if not defined VS_YEAR exit /b 0
set "GENERATOR=Visual Studio %VS_MAJOR% %VS_YEAR%"
exit /b 0
