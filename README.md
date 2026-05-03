# Interfacing Claude on a TI-84 Plus CE Calculator

This project lets you talk to Anthropic's Claude API from a TI-84 Plus CE
calculator. The calculator runs a small C program that reads prompts from
the keypad and ships them to a host computer over USB serial; the host
calls the Claude API and streams the reply back to the calculator a chunk
at a time.

Originally a ChatGPT bridge - now uses Claude, with a `calculate` tool so
the model does real arithmetic on demand instead of guessing.

## Quick start (Windows)

If you just want it working, double-click **`setup.bat`** in this folder.
It will:

 1. Install the Python packages (`anthropic`, `pyserial`).
 2. Download and install the CE C/C++ Toolchain (~100 MB) under
    `%USERPROFILE%\CEdev`, no admin needed.
 3. Build `CALCGPT.8xp`.
 4. Optionally save your `ANTHROPIC_API_KEY` for your user.

You still need Python 3.9+ installed beforehand
(<https://www.python.org/downloads/>, tick "Add python.exe to PATH"), and
[TI Connect CE](https://education.ti.com/en/software/details/en/CA9C74CAD02440A69FDC7189D7E1B6C2/swticonnectcesoftware)
to copy `CALCGPT.8xp` onto the calculator (a one-time GUI drag-and-drop
that no script can do for you).

After that, plug in the calculator, run `Asm(CALCGPT)` from the catalog,
and start `run.bat` on your PC.

The rest of this README covers what the helpers actually do, manual
setup, and configuration.

## Helper scripts

| File | What it does |
| --- | --- |
| `setup.bat` | One-click: deps + toolchain + build + API key prompt. |
| `install.bat` | Just the Python deps step. |
| `setup-toolchain.bat` | Just the CE C/C++ Toolchain download. Pass `-y` to skip the confirmation prompt. |
| `build.bat` | Compile `CALCGPT.8xp`. `build.bat clean` removes build artifacts. |
| `run.bat` | Launch the host script (`DesktopProgram\calcgpt.py`). |

## Features

 - **Three input modes** cycled by the **ALPHA** key: numeric/math (digits
   plus `+ - * / ^ ( ) = . , space`), UPPERCASE letters, and lowercase.
   Current mode is shown at the top of the input screen.
 - **DEL** = backspace. **MODE** cancels input. **CLEAR** also acts as
   backspace.
 - The prompt you typed is echoed into the scrollback so you can see your
   question alongside the reply.
 - **Animated spinner** in the corner while you wait for a response.
 - **`calculate` tool** - Claude calls a real Python evaluator for any
   arithmetic, so you get exact answers instead of LLM-guess math.
 - **Streaming** replies appear on the calculator as they're generated.
 - **Auto-detect** of the calculator's COM port via USB VID/PID; manual
   port picker only kicks in if auto-detect fails.
 - **Auto-reconnect** if the calc is unplugged or the cable is bumped.
 - **JSONL conversation log** written to `calcgpt-log-YYYY-MM-DD.jsonl`
   in the working directory.
 - **Prompt caching** is applied to the system prompt for cost savings on
   long sessions (no-op if the prefix is below the model's cache
   threshold).
 - `/reset` (or `clear`) sent as a prompt wipes the conversation history
   without restarting.

## Manual installation

If you'd rather not run `setup.bat`, here's what it does step-by-step.

You'll need:

 - **Python 3.9+** with two packages:
   ```
   pip install anthropic pyserial
   ```
 - **An Anthropic API key.** Set it in your environment as
   `ANTHROPIC_API_KEY` before running the host script.
 - **CE C/C++ Toolchain** to compile `CALCGPT.8xp` (see below).
 - **TI Connect CE software** to transfer the program onto the
   calculator (available from the TI website).

### Building the .8xp manually

A `.8xp` file is the format the TI calculator loads. There's no online
service that compiles C to `.8xp` - you need the CE C/C++ Toolchain
installed locally.

**One-time setup:**

 1. Download a release of the
    [CE C/C++ Toolchain](https://github.com/CE-Programming/toolchain/releases)
    (e.g. `CEdev-Win.zip` on Windows). Or just run
    `setup-toolchain.bat`, which does this and the next two steps for
    you.
 2. Extract it, e.g. to `C:\CEdev`.
 3. Point a `CEDEV` environment variable at that folder. From an
    administrator PowerShell:
    ```powershell
    setx CEDEV "C:\CEdev"
    ```
    Open a new terminal afterwards so the variable is visible.

**Build:**

```
build.bat
```

This produces `CalculatorProgram\calcgpt\bin\CALCGPT.8xp`. Transfer that
file to the calculator with TI Connect CE.

`build.bat clean` removes build artifacts. Any other argument is passed
through to `make`.

## Usage

 1. Connect the calculator to your computer via USB.
 2. Transfer `CALCGPT.8xp` to the calculator using TI Connect&trade; CE.
 3. On the calculator: open the catalog (**2nd**, then **0**), choose
    `Asm(`, then pick `CALCGPT` and press enter twice. The screen will
    print "Ready" once it's listening on USB.
 4. Set your API key (skip this if `setup.bat` already saved it):
    ```powershell
    # Windows PowerShell
    $env:ANTHROPIC_API_KEY = "sk-ant-..."
    ```
    ```bash
    # macOS / Linux
    export ANTHROPIC_API_KEY=sk-ant-...
    ```
 5. Run the host:
    ```
    run.bat
    ```
    or directly:
    ```
    python DesktopProgram/calcgpt.py
    ```
    It auto-detects the calculator on most systems; if not, pick the
    correct port from the list.
 6. The calculator switches to "Connected!" once the host writes its
    handshake byte.
 7. Press **2nd** to open the input screen. Type with the alpha keys.
    Press **ALPHA** to cycle between `[123]`, `[ABC]`, and `[abc]`. Press
    **DEL** to backspace, **MODE** to cancel, **enter** to send.
 8. The reply streams back. Use **up/down arrows** to scroll older pages.
 9. Send `/reset` (or `clear`) as a prompt to wipe the conversation.

## Configuration

The constants at the top of `DesktopProgram/calcgpt.py` are the main
knobs:

 - `MODEL` - defaults to `claude-haiku-4-5` (fast and cheap, plenty for
   short calculator-screen replies). Swap to `claude-sonnet-4-6` or
   `claude-opus-4-7` for higher-quality replies at higher cost.
 - `MAX_TOKENS` - caps the response length.
 - `SYSTEM_PROMPT` - tells Claude to keep replies short, ASCII-only, and
   to use the `calculate` tool for any arithmetic.
 - `TOOLS` - the tool definitions exposed to Claude. Add your own here.

## Notes

 - Tested on a TI-84 Plus CE running TIOS 5.1. Other models / firmware
   may need changes.
 - The calculator's serial buffer is 64 bytes, so the host writes at
   most 63 bytes per chunk with a small inter-chunk delay.
 - Smart quotes, em-dashes, and other unicode coming back from Claude are
   folded to ASCII before being written to the calculator so they don't
   render as garbage.
 - The `calculate` tool runs Python `eval` against a sandboxed namespace
   (no `__builtins__`, only `math` plus a few built-ins). It's not a full
   Python REPL, but it handles arithmetic, trig, logs, factorials, etc.
