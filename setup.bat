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
REM Step 4: Anthropic API key (required)
REM ------------------------------------------------------------
echo.
echo --- 4/4: Anthropic API key ---
echo.
echo The host needs an Anthropic API key to talk to Claude. If you
echo already have one set on this user, you can keep it.
echo.

if not "%ANTHROPIC_API_KEY%"=="" (
    echo ANTHROPIC_API_KEY is already set on this user.
    set "REPLACE="
    set /p REPLACE="Replace it with a new key? [y/N] "
    if /I not "!REPLACE!"=="y" (
        echo Keeping existing key.
        goto :api_key_done
    )
)

echo.
echo To get an API key:
echo   1. Open https://console.anthropic.com/settings/keys in your browser.
echo   2. Sign in (or create an account if you don't have one).
echo   3. Click "Create Key", give it a name, click Create.
echo   4. Copy the key - it starts with "sk-ant-" and is shown ONLY ONCE.
echo.
echo You will need a small amount of credit on your Anthropic account
echo to actually send messages. New accounts often get a small free
echo credit; otherwise add a few dollars under Billing.
echo.
pause

:api_key_prompt
echo.
set "APIKEY="
set /p APIKEY="Paste your API key here: "

if "!APIKEY!"=="" (
    echo An API key is required to continue.
    set "RETRY="
    set /p RETRY="Try again? [Y/n] "
    if /I "!RETRY!"=="n" (
        echo.
        echo Skipped. The host will not run until you set ANTHROPIC_API_KEY.
        echo Set it later with:
        echo     setx ANTHROPIC_API_KEY "sk-ant-..."
        echo and open a NEW terminal so it takes effect.
        goto :api_key_done
    )
    goto :api_key_prompt
)

echo !APIKEY! | findstr /b /c:"sk-ant-" >nul
if errorlevel 1 (
    echo.
    echo Warning: that doesn't look like an Anthropic API key
    echo (expected to start with "sk-ant-").
    set "CONFIRM="
    set /p CONFIRM="Save it anyway? [y/N] "
    if /I not "!CONFIRM!"=="y" goto :api_key_prompt
)

setx ANTHROPIC_API_KEY "!APIKEY!" >nul
if errorlevel 1 (
    echo Failed to save API key.
) else (
    echo Saved. You will need to open a NEW terminal window before
    echo running run.bat so the variable is visible.
)

:api_key_done

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
echo Remaining steps (manual - these need a GUI / the calculator):
echo.
echo   1. Install TI Connect CE on your PC:
echo        https://education.ti.com/  (search "TI Connect CE software")
echo.
echo   2. Install Cesium on your calculator. Cesium is a free shell
echo      that can launch CE C programs (modern TI-OS blocks the old
echo      Asm( command, so this step is required):
echo        a. Download Cesium from https://www.cemetech.net/downloads/files/1244
echo           - you want the file named "Cesium.8xk" (or similar).
echo        b. Plug the calculator in via USB.
echo        c. Open TI Connect CE and drag Cesium.8xk onto the calc.
echo.
echo   3. Send the program to the calculator:
echo      Drag CALCGPT.8xp from the path above onto the calc in
echo      TI Connect CE.
echo.
echo   4. On the calculator:
echo        a. Press [apps], pick "Cesium", press [enter].
echo        b. Use arrow keys to highlight CALCGPT, press [enter].
echo        c. Screen should print "Ready".
echo.
echo   5. On your PC, open a NEW terminal in this folder and run:
echo        run.bat
echo      (New terminal because setx only affects future shells.)
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
