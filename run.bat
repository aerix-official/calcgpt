@echo off
setlocal

REM Launch the calcGPT host script (DesktopProgram\calcgpt.py).
REM Requires ANTHROPIC_API_KEY in the environment and the deps from install.bat.

echo === calcGPT host ===
echo.

call :find_python
if "%PY%"=="" (
    echo ERROR: Python is not installed or not on PATH.
    echo Install Python 3.9+ from https://www.python.org/downloads/
    set "RC=1"
    goto :end
)

if "%ANTHROPIC_API_KEY%"=="" (
    echo ERROR: ANTHROPIC_API_KEY is not set.
    echo Set it for this session:
    echo     set ANTHROPIC_API_KEY=sk-ant-...
    echo Or persist it across shells:
    echo     setx ANTHROPIC_API_KEY "sk-ant-..."
    echo   ^(then open a new terminal^)
    set "RC=1"
    goto :end
)

%PY% "%~dp0DesktopProgram\calcgpt.py" %*
set "RC=%ERRORLEVEL%"

:end
echo.
pause
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
