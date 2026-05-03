@echo off
setlocal

REM Install the Python dependencies needed by DesktopProgram\calcgpt.py.
REM Run this once after cloning, or whenever you want to upgrade them.

echo === calcGPT dependency install ===
echo.

call :find_python
if "%PY%"=="" (
    echo ERROR: Python is not installed or not on PATH.
    echo Install Python 3.9+ from https://www.python.org/downloads/
    echo and tick "Add python.exe to PATH" during install.
    set "RC=1"
    goto :end
)

echo Using: %PY%
%PY% --version
echo.

echo Upgrading pip...
%PY% -m pip install --upgrade pip
if errorlevel 1 (
    echo Failed to upgrade pip.
    set "RC=1"
    goto :end
)

echo.
echo Installing anthropic and pyserial...
%PY% -m pip install --upgrade anthropic pyserial
if errorlevel 1 (
    echo Failed to install dependencies.
    set "RC=1"
    goto :end
)

echo.
echo Done. Set ANTHROPIC_API_KEY and run "run.bat" to start the host.
set "RC=0"
goto :end


:end
echo.
if not "%CALCGPT_SKIP_PAUSE%"=="1" pause
endlocal & exit /b %RC%


:find_python
set "PY="
where py >nul 2>nul
if %ERRORLEVEL%==0 (
    set "PY=py -3"
    exit /b 0
)
where python >nul 2>nul
if %ERRORLEVEL%==0 (
    set "PY=python"
    exit /b 0
)
exit /b 0
