@echo off
setlocal EnableExtensions
cd /d "%~dp0"

where cmake >nul 2>&1
if errorlevel 1 (
  echo cmake was not found on PATH.
  exit /b 1
)

set "VS_CHOICE="
if /I "%~1"=="2017" set "VS_CHOICE=%~1" & shift
if /I "%~1"=="2019" set "VS_CHOICE=%~1" & shift
if /I "%~1"=="2022" set "VS_CHOICE=%~1" & shift
if /I "%~1"=="2026" set "VS_CHOICE=%~1" & shift
set "PTLIB_DIR=%~1"
if not "%PTLIB_DIR%"=="" goto have_ptlib
set "PTLIB_DIR=%~dp0..\ptlib\build"
if exist "%PTLIB_DIR%\PTLibConfig.cmake" goto have_ptlib
echo PTLib was not found at %PTLIB_DIR%
echo Build PTLib first, or pass its build directory or install prefix:
echo   configure_vs.bat C:\ptlib\build
exit /b 1

:have_ptlib
call :generator "%VS_CHOICE%"

set "TOOLCHAIN="
if defined VCPKG_ROOT if exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
  set TOOLCHAIN=-DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
)
if not defined TOOLCHAIN echo VCPKG_ROOT is not set. CMake will look for vcpkg.

echo Configuring OPAL with "%GENERATOR%"
echo PTLib: %PTLIB_DIR%
rem OPAL_INCLUDE_PTLIB_PROJECTS lists that build's vcxproj files in this solution.
cmake -S . -B build -G "%GENERATOR%" -A x64 -DOPAL_PTLIB_DIR="%PTLIB_DIR%" -DOPAL_INCLUDE_PTLIB_PROJECTS=ON %TOOLCHAIN%
if errorlevel 1 exit /b 1

echo.
if exist "%CD%\build\OPAL.slnx" (
  echo Created %CD%\build\OPAL.slnx
  echo Visual Studio 2026 opens this .slnx file. Visual Studio 2022 does not.
) else if exist "%CD%\build\OPAL.sln" (
  echo Created %CD%\build\OPAL.sln
) else (
  echo CMake finished but did not write build\OPAL.sln or build\OPAL.slnx.
  exit /b 1
)
exit /b 0

:generator
if /I "%~1"=="2017" set "GENERATOR=Visual Studio 15 2017" & exit /b 0
if /I "%~1"=="2019" set "GENERATOR=Visual Studio 16 2019" & exit /b 0
if /I "%~1"=="2022" set "GENERATOR=Visual Studio 17 2022" & exit /b 0
if /I "%~1"=="2026" set "GENERATOR=Visual Studio 18 2026" & exit /b 0
if not "%~1"=="" set "GENERATOR=%~1" & exit /b 0

rem installationVersion and catalog_productLineVersion are both 18 for the
rem newest Visual Studio. CMake's generator name is "Visual Studio 18 2026".
set "GENERATOR=Visual Studio 17 2022"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 0
set "VS_LINE="
for /f "usebackq tokens=1 delims=." %%V in (`"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property catalog_productLineVersion`) do set "VS_LINE=%%V"
if "%VS_LINE%"=="15" set "GENERATOR=Visual Studio 15 2017"
if "%VS_LINE%"=="16" set "GENERATOR=Visual Studio 16 2019"
if "%VS_LINE%"=="17" set "GENERATOR=Visual Studio 17 2022"
if "%VS_LINE%"=="18" set "GENERATOR=Visual Studio 18 2026"
exit /b 0
