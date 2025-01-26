@echo off

set PLATFORM=x64
set CONFIGURATION=Debug
set PTLIBPLUGINDIR=..\..\bin\plugins\%PLATFORM%\%CONFIGURATION%
if "%PATH:..\..\..\ptlib\lib=nothing%"=="%PATH%" PATH=%PTLIBPLUGINDIR%;..\..\lib;..\..\..\ptlib\lib;%VCPKG_ROOT%\installed\%PLATFORM%-windows\bin;%EXTERNALDIR%\Lua-win64;%EXTERNALDIR%\ffmpeg-win64-dev\bin;%EXTERNALDIR%\aws-sdk-cpp\build\install\x64-Debug\bin;C:\Program Files\OpenSSL-Win64\bin;%PATH%
set FAXOPAL="..\..\bin\Fax Application\%PLATFORM%\%CONFIGURATION%\faxopal.exe"
if not exist %FAXOPAL% (
	echo No executable available for faxopal: %FAXOPAL%
	exit /b 1
)
%FAXOPAL% >nul
if %errorlevel% neq 1 (
	echo Cannot run executable: %FAXOPAL%
	path
	exit /b 1
)

set HOST=127.0.0.1

set RESULT_DIR=.\results
if not exist %RESULT_DIR% mkdir %RESULT_DIR%

set RESULT_PREFIX=%RESULT_DIR%\%1%_%2%_%3%_
if not "%4"=="" set RESULT_PREFIX=%RESULT_PREFIX%%4%_

set TX_ARG=--output %RESULT_PREFIX%tx.log
set RX_ARG=--output %RESULT_PREFIX%rx.log
set XX_ARG=--no-lid --no-capi --no-sdp --timeout 2:30 --trace-level 5 --trace-option +file+date-time

if "%1"=="sip" (
	set TX_ARG=%TX_ARG% --sip %HOST%:25060
	set RX_ARG=%RX_ARG% --sip %HOST%:15060
	set XX_ARG=%XX_ARG% --no-h323
	set DEST_URL=sip:%HOST%:15060
) else (
	set TX_ARG=%TX_ARG% --h323 %HOST%:21720
	set RX_ARG=%RX_ARG% --h323 %HOST%:11720
	set XX_ARG=%XX_ARG% --no-sip
	set DEST_URL=h323:%HOST%:11720
)

if "%2"=="g711" (
	set TX_ARG=%TX_ARG% --audio
) else if "%2"== "rtp" (
	set TX_ARG=%TX_ARG% --RTP
)

if "%3"=="g711" (
	set RX_ARG=%RX_ARG% --audio
) else if "%3"=="rtp" (
	set RX_ARG=%RX_ARG% --RTP
)

if "%4"=="slow" set XX_ARG=%XX_ARG% --no-fast

if "%2"=="t38" if "%3"=="g711" set TX_ARG=%TX_ARG% --switch-on-ced

if "%2"=="t38" if "%3"=="t38" set XX_ARG=%XX_ARG% --no-fallback

set TX_ARG=%TX_ARG% .\F06_200.tif %DEST_URL%
set RX_ARG=%RX_ARG% %RESULT_PREFIX%rx.tif

set TX_OUT=%RESULT_PREFIX%tx.out
set RX_OUT=%RESULT_PREFIX%rx.out

echo %FAXOPAL% %XX_ARG% %TX_ARG% > %TX_OUT%
echo %FAXOPAL% %XX_ARG% %RX_ARG% > %RX_OUT%

start "RxFax" /b cmd.exe /c %FAXOPAL% %XX_ARG% %RX_ARG% ^< nul ^>^> %RX_OUT% ^2^>^&^1
timeout /nobreak /t 2 >nul
start "TxFax" /b cmd.exe /c %FAXOPAL% %XX_ARG% %TX_ARG% ^< nul ^>^> %TX_OUT% ^2^>^&^1

echo Performing test:%4 %1, %2 -^> %3 ...

:wait
tasklist | find /i "faxopal" >nul 2>&1
if not errorlevel 1 (
	timeout /nobreak /t 2 >nul
	goto wait
)

find "Success" %TX_OUT% >nul 2>&1
if not errorlevel 1 find "Success" %RX_OUT% >nul 2>&1
if errorlevel 1 (
	echo FAILED!
) else (
	echo Successful.
)
