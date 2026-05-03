@echo off
setlocal

REM Wrapper so you can double-click. Real work lives in setup-toolchain.ps1.
REM Pass -y (or set CALCGPT_AUTO_YES=1) to skip the confirmation prompt.

set "PSARGS="
if /I "%~1"=="-y"   set "PSARGS=-Yes"
if /I "%~1"=="--yes" set "PSARGS=-Yes"
if "%CALCGPT_AUTO_YES%"=="1" set "PSARGS=-Yes"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup-toolchain.ps1" %PSARGS%
set "RC=%ERRORLEVEL%"

echo.
if not "%CALCGPT_SKIP_PAUSE%"=="1" pause
endlocal & exit /b %RC%
