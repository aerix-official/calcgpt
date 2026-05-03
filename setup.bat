@echo off
setlocal enabledelayedexpansion

REM ============================================================
REM   calcGPT one-click setup
REM ============================================================
REM   Double-click this file to:
REM     1. Install Python deps (anthropic, pyserial)
REM     2. Download + install the CE C/C++ Toolchain (if missing)
REM     3. Build CALCGPT.8xp
REM     4. Optionally save your Anthropic API key
REM
REM   After this finishes you only have to:
REM     - Open TI Connect CE and drag CALCGPT.8xp onto the calc
REM     - Run run.bat
REM ============================================================

set "CALCGPT_SKIP_PAUSE=1"
set "ROOT=%~dp0"

echo === calcGPT one-click setup ===
echo.
echo This script will install everything you need to talk to Claude
echo from a TI-84 Plus CE. It uses ~100 MB of disk space and takes
echo a couple of minutes on a typical broadband connection.
echo.

set "ANSWER="
set /p ANSWER="Continue? [Y/n] "
if /I "!ANSWER!"=="n" goto :aborted
if /I "!ANSWER!"=="no" goto :aborted

REM ------------------------------------------------------------
REM Step 0: sanity-check Python before doing anything else
REM ------------------------------------------------------------
echo.
echo --- Checking Python ---
where py >nul 2>nul
if %ERRORLEVEL%==0 goto :py_ok
where python >nul 2>nul
if %ERRORLEVEL%==0 goto :py_ok

echo.
echo ERROR: Python is not installed or not on PATH.
echo.
echo Install Python 3.9+ from https://www.python.org/downloads/
echo and tick "Add python.exe to PATH" during install. Then re-run
echo this script.
goto :failed

:py_ok
echo Python: OK

REM ------------------------------------------------------------
REM Step 1: Python deps
REM ------------------------------------------------------------
echo.
echo --- 1/4: Installing Python packages ---
call "%ROOT%install.bat"
if errorlevel 1 (
    echo.
    echo Python dependency install failed.
    goto :failed
)

REM ------------------------------------------------------------
REM Step 2: CE toolchain (skip if CEDEV already set and valid)
REM ------------------------------------------------------------
echo.
echo --- 2/4: CE C/C++ Toolchain ---

set "CEDEV_VALID="
if not "%CEDEV%"=="" (
    if exist "%CEDEV%\bin\make.exe" set "CEDEV_VALID=1"
)

if defined CEDEV_VALID (
    echo Found existing CEDEV at %CEDEV% - skipping toolchain install.
) else (
    echo Toolchain not found. Downloading and installing...
    call "%ROOT%setup-toolchain.bat" -y
    if errorlevel 1 (
        echo.
        echo Toolchain install failed.
        goto :failed
    )
    REM setx writes to the registry but does NOT update the current
    REM shell. Pull the new value out of HKCU\Environment so build.bat
    REM (called below) can see it.
    for /f "tokens=2,*" %%A in ('reg query "HKCU\Environment" /v CEDEV 2^>nul ^| findstr /I "CEDEV"') do set "CEDEV=%%B"
    if "!CEDEV!"=="" (
        echo.
        echo ERROR: setup-toolchain ran but CEDEV is still empty.
        goto :failed
    )
    echo CEDEV is now: !CEDEV!
)

REM ------------------------------------------------------------
REM Step 3: build CALCGPT.8xp
REM ------------------------------------------------------------
echo.
echo --- 3/4: Building CALCGPT.8xp ---
call "%ROOT%build.bat"
if errorlevel 1 (
    echo.
    echo Build failed.
    goto :failed
)

REM ------------------------------------------------------------
REM Step 4: Anthropic API key (optional)
REM ------------------------------------------------------------
echo.
echo --- 4/4: Anthropic API key ---
if not "%ANTHROPIC_API_KEY%"=="" (
    echo ANTHROPIC_API_KEY is already set in this session - skipping.
) else (
    echo.
    echo You need an Anthropic API key to run the host. Get one at:
    echo   https://console.anthropic.com/
    echo.
    echo Paste it now to save it permanently for your user, or just
    echo press Enter to skip and set it yourself later.
    echo.
    set "APIKEY="
    set /p APIKEY="API key (leave blank to skip): "
    if not "!APIKEY!"=="" (
        setx ANTHROPIC_API_KEY "!APIKEY!" >nul
        if errorlevel 1 (
            echo Failed to save API key.
        ) else (
            echo Saved. Open a NEW terminal window before running run.bat
            echo so the variable is visible.
        )
    ) else (
        echo Skipped. Set it later with:
        echo     setx ANTHROPIC_API_KEY "sk-ant-..."
    )
)

REM ------------------------------------------------------------
REM Done
REM ------------------------------------------------------------
echo.
echo ============================================================
echo   Setup complete.
echo ============================================================
echo.
echo Built file:
echo   %ROOT%CalculatorProgram\calcgpt\bin\CALCGPT.8xp
echo.
echo Remaining steps (manual - they need a GUI):
echo   1. Install TI Connect CE from https://education.ti.com/
echo      (search "TI Connect CE software")
echo   2. Plug your TI-84 Plus CE in via USB.
echo   3. Open TI Connect CE and drag CALCGPT.8xp onto the calc.
echo   4. On the calc: 2nd, 0 (catalog), select Asm( , then pick
echo      CALCGPT and press Enter twice. Screen should say "Ready".
echo   5. Open a NEW terminal in this folder and run:  run.bat
echo.
set "RC=0"
goto :end

:aborted
echo Aborted.
set "RC=1"
goto :end

:failed
echo.
echo Setup did not complete. See messages above.
set "RC=1"

:end
echo.
pause
endlocal & exit /b %RC%
