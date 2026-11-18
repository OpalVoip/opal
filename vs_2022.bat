@echo off

set "BUILD_DIR=build_2022"
set "VCPKG_DIR=C:\src\vcpkg"

setlocal EnableExtensions
cd /d "%~dp0"

where cmake >nul 2>&1
if errorlevel 1 (
  echo cmake was not found on PATH.
  exit /b 1
)
where git >nul 2>&1
if errorlevel 1 (
  echo git was not found on PATH.
  exit /b 1
)

set "PTLIB_DIR=%~1"
if not "%PTLIB_DIR%"=="" goto have_ptlib
set "PTLIB_DIR=%~dp0..\ptlib\build_2022"
if exist "%PTLIB_DIR%\PTLibConfig.cmake" goto have_ptlib
set "PTLIB_DIR=%~dp0..\ptlib\build"
if exist "%PTLIB_DIR%\PTLibConfig.cmake" goto have_ptlib
echo PTLib was not found. Run ptlib\vs_2022.bat first, or pass its build directory:
echo   vs_2022.bat C:\ptlib\build_2022
exit /b 1
:have_ptlib

rem The vcpkg shipped with Visual Studio 2022 does not implement
rem REMOVE_DUPLICATES. Prefer VCPKG_ROOT when that checkout is current,
rem and otherwise use C:\src\vcpkg.
if not defined VCPKG_ROOT goto have_vcpkg_dir
if not exist "%VCPKG_ROOT%\scripts\cmake\vcpkg_host_path_list.cmake" goto have_vcpkg_dir
findstr /C:"REMOVE_DUPLICATES" "%VCPKG_ROOT%\scripts\cmake\vcpkg_host_path_list.cmake" >nul
if not errorlevel 1 set "VCPKG_DIR=%VCPKG_ROOT%"
:have_vcpkg_dir

if exist "%VCPKG_DIR%\scripts\buildsystems\vcpkg.cmake" goto vcpkg_present
echo Cloning vcpkg into %VCPKG_DIR%
if not exist "%VCPKG_DIR%\.." mkdir "%VCPKG_DIR%\.."
git clone https://github.com/microsoft/vcpkg.git "%VCPKG_DIR%"
if errorlevel 1 exit /b 1
:vcpkg_present

findstr /C:"REMOVE_DUPLICATES" "%VCPKG_DIR%\scripts\cmake\vcpkg_host_path_list.cmake" >nul
if not errorlevel 1 goto vcpkg_current
echo vcpkg at %VCPKG_DIR% does not implement REMOVE_DUPLICATES.
echo Update it with: git -C "%VCPKG_DIR%" pull
exit /b 1
:vcpkg_current

if exist "%VCPKG_DIR%\vcpkg.exe" goto vcpkg_bootstrapped
echo Bootstrapping vcpkg
call "%VCPKG_DIR%\bootstrap-vcpkg.bat" -disableMetrics
if errorlevel 1 exit /b 1
:vcpkg_bootstrapped

if not exist "%BUILD_DIR%\CMakeCache.txt" goto configure
findstr /I /C:"%VCPKG_DIR:\=/%/scripts/buildsystems/vcpkg.cmake" "%BUILD_DIR%\CMakeCache.txt" >nul
if not errorlevel 1 goto configure
echo Removing %BUILD_DIR% because it was configured with a different vcpkg.
rmdir /s /q "%BUILD_DIR%"

:configure
echo Configuring OPAL with Visual Studio 17 2022
echo PTLib: %PTLIB_DIR%
echo vcpkg: %VCPKG_DIR%
cmake -S . -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -DVCPKG_TARGET_TRIPLET=x64-windows -DOPAL_PTLIB_DIR="%PTLIB_DIR%" -DOPAL_INCLUDE_PTLIB_PROJECTS=ON -DCMAKE_TOOLCHAIN_FILE="%VCPKG_DIR%\scripts\buildsystems\vcpkg.cmake"
if errorlevel 1 exit /b 1

echo.
echo Created %CD%\%BUILD_DIR%\OPAL.sln
exit /b 0
