@echo off
setlocal

REM Build calcGPT for the TI-84 Plus CE.
REM
REM Requires the CE C/C++ Toolchain installed and the CEDEV environment
REM variable pointing at it. Download:
REM   https://github.com/CE-Programming/toolchain/releases
REM
REM Usage:
REM   build.bat           - compile, output bin\CALCGPT.8xp
REM   build.bat clean     - remove build artifacts
REM   build.bat <target>  - any other make target

echo === calcGPT build ===
echo.

if "%CEDEV%"=="" (
    echo ERROR: CEDEV environment variable is not set.
    echo.
    echo 1. Download the CE C/C++ Toolchain from
    echo    https://github.com/CE-Programming/toolchain/releases
    echo 2. Extract it ^(e.g. to C:\CEdev^).
    echo 3. Set CEDEV to that folder, e.g.:
    echo      setx CEDEV "C:\CEdev"
    echo    Then open a NEW terminal and run this script again.
    set "RC=1"
    goto :end
)

echo CEDEV=%CEDEV%

if not exist "%CEDEV%\bin\make.exe" (
    echo.
    echo ERROR: %CEDEV%\bin\make.exe not found.
    echo Verify CEDEV points at your CEdev install root.
    set "RC=1"
    goto :end
)

set "PATH=%CEDEV%\bin;%PATH%"

echo.
echo Running make in CalculatorProgram\calcgpt ...
echo.

pushd "%~dp0CalculatorProgram\calcgpt"
make %*
set "RC=%ERRORLEVEL%"
popd

if not "%RC%"=="0" (
    echo.
    echo Build failed with exit code %RC%.
    goto :end
)

echo.
echo Build succeeded. Output files:
if exist "%~dp0CalculatorProgram\calcgpt\bin\*.8xp" (
    dir /b "%~dp0CalculatorProgram\calcgpt\bin\*.8xp"
    echo.
    echo Transfer the .8xp file to your calculator with TI Connect CE
    echo or copy it through Cesium.
) else (
    echo   ^(no .8xp produced - expected if you ran "clean"^)
)

:end
echo.
if not "%CALCGPT_SKIP_PAUSE%"=="1" pause
endlocal & exit /b %RC%
