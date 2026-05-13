<div align="center">

# 🧮 Interfacing Claude on a TI-84 Plus CE Calculator

[![Releases](https://img.shields.io/badge/Releases-Latest-blue?style=flat-square&logo=github)](https://github.com/aerix-official/calcgpt/releases)
[![License](https://img.shields.io/badge/License-MIT-green?style=flat-square)](https://github.com/aerix-official/calcgpt/blob/main/LICENSE)
[![Downloads](https://img.shields.io/github/downloads/aerix-official/calcgpt/total?style=flat-square&label=Downloads&color=orange)](https://github.com/aerix-official/calcgpt/releases)

*This project lets you talk to Anthropic's Claude API from a TI-84 Plus CE calculator. The calculator runs a small C program that reads prompts from the keypad and ships them to a host computer over USB serial; the host calls the Claude API and streams the reply back to the calculator a chunk at a time.*

[Features](#-features) • [Quick Start](#-quick-start-windows) • [Helper Scripts](#-helper-scripts) • [Manual Installation](#-manual-installation) • [Usage](#-usage) • [Configuration](#-configuration) • [Notes](#-notes)

</div>

---

> # ⚠️ IMPORTANT DISCLAIMER — READ BEFORE USE
>
> ### **This project interfaces with Anthropic's Claude API using your own API key.**
>
> Anthropic's Terms of Service apply to all API usage. You are responsible for any costs incurred from API calls, and for ensuring your prompts comply with their Acceptable Use Policy. The `calculate` tool evaluates arithmetic in a sandboxed Python environment, but you should not send sensitive data or untrusted code.
>
> - **You are solely responsible** for how you use this software and any API costs.
> - **The author accepts no liability** for any consequences arising from use or misuse, including API costs, data exposure, or violations of any third-party Terms of Service (Anthropic, etc.).
> - **Secure your API key** — store it safely and never share it. Rotate keys if compromised.
> - This project is an **independent experiment** and is **not affiliated with, endorsed by, or sponsored by** Anthropic or Texas Instruments.
>
> **By downloading, installing, or running this software, you acknowledge that you have read and accepted the above terms.**

---

## 🚀 Features

- **Three input modes** cycled by the **ALPHA** key: numeric/math (digits plus `+ - * / ^ ( ) = . , space`), UPPERCASE letters, and lowercase. Current mode shown at the top of the input screen.
- **DEL** = backspace. **MODE** cancels input. **CLEAR** also acts as backspace.
- The prompt you typed is echoed into the scrollback so you can see your question alongside the reply.
- **Animated spinner** in the corner while you wait for a response.
- **`calculate` tool** - Claude calls a real Python evaluator for any arithmetic, so you get exact answers instead of LLM-guess math.
- **Streaming** replies appear on the calculator as they're generated.
- **Auto-detect** of the calculator's COM port via USB VID/PID; manual port picker only kicks in if auto-detect fails.
- **Auto-reconnect** if the calc is unplugged or the cable is bumped.
- **JSONL conversation log** written to `calcgpt-log-YYYY-MM-DD.jsonl` in the working directory.
- **Prompt caching** is applied to the system prompt for cost savings on long sessions (no-op if the prefix is below the model's cache threshold).
- `/reset` (or `clear`) sent as a prompt wipes the conversation history without restarting.

---

## 🏃 Quick Start (Windows)

If you just want it working, double-click **`setup.bat`** in this folder. It will:

1. Install the Python packages (`anthropic`, `pyserial`).
2. Download and install the CE C/C++ Toolchain (~100 MB) under `%USERPROFILE%\CEdev`, no admin needed.
3. Build `CALCGPT.8xp`.
4. Walk you through getting an Anthropic API key and save it for your user (the host can't run without one).

Before running it you need Python 3.9+ (<https://www.python.org/downloads/>, tick "Add python.exe to PATH").

After `setup.bat` finishes, you still need to do these by hand because they happen on the calculator / in a GUI:

1. Install [TI Connect CE](https://education.ti.com/en/software/details/en/CA9C74CAD02440A69FDC7189D7E1B6C2/swticonnectcesoftware) on your PC - this is the program that moves files onto the calc.
2. **Install Cesium on your calculator.** Modern TI-OS blocks the old `Asm(` command, so a CE C program will not run from the catalog. Cesium is a free shell that launches them.
   - Download `Cesium.8xk` from <https://www.cemetech.net/downloads/files/1244>
   - Plug the calc in via USB, open TI Connect CE, drag `Cesium.8xk` onto the calculator.
3. Drag `CalculatorProgram\calcgpt\bin\CALCGPT.8xp` onto the calc in TI Connect CE.
4. On the calc: press `[apps]`, choose `Cesium`, highlight `CALCGPT`, press `[enter]`. The screen should say "Ready".
5. Open a **new** terminal in this folder and run `run.bat`.

The rest of this README covers what the helpers actually do, manual setup, and configuration.

---

## 📋 Helper Scripts

| File | What it does |
| --- | --- |
| `setup.bat` | One-click: deps + toolchain + build + API key prompt. |
| `install.bat` | Just the Python deps step. |
| `setup-toolchain.bat` | Just the CE C/C++ Toolchain download. Pass `-y` to skip the confirmation prompt. |
| `build.bat` | Compile `CALCGPT.8xp`. `build.bat clean` removes build artifacts. |
| `run.bat` | Launch the host script (`DesktopProgram/calcgpt.py`). |

---

## 🔧 Manual Installation

If you'd rather not run `setup.bat`, here's what it does step-by-step.

You'll need:

- **Python 3.9+** with two packages:
  ```
  pip install anthropic pyserial
  ```
- **An Anthropic API key.** Set it in your environment as `ANTHROPIC_API_KEY` before running the host script.
- **CE C/C++ Toolchain** to compile `CALCGPT.8xp` (see below).
- **TI Connect CE software** to transfer the program onto the calculator (available from the TI website).

### Building the .8xp manually

A `.8xp` file is the format the TI calculator loads. There's no online service that compiles C to `.8xp` - you need the CE C/C++ Toolchain installed locally.

**One-time setup:**

1. Download a release of the [CE C/C++ Toolchain](https://github.com/CE-Programming/toolchain/releases) (e.g. `CEdev-Win.zip` on Windows). Or just run `setup-toolchain.bat`, which does this and the next two steps for you.
2. Extract it, e.g. to `C:\CEdev`.
3. Point a `CEDEV` environment variable at that folder. From an administrator PowerShell:
   ```powershell
   setx CEDEV "C:\CEdev"
   ```
   Open a new terminal afterwards so the variable is visible.

**Build:**

```
build.bat
```

This produces `CalculatorProgram\calcgpt\bin\CALCGPT.8xp`. Transfer that file to the calculator with TI Connect CE.

`build.bat clean` removes build artifacts. Any other argument is passed through to `make`.

---

## 📖 Usage

1. Make sure **Cesium** is installed on the calculator (see Quick start step 2). On modern TI-OS the old `Asm(` command is blocked, so Cesium is what actually launches CE C programs.
2. Connect the calculator to your computer via USB.
3. Transfer `CALCGPT.8xp` to the calculator using TI Connect&trade; CE.
4. On the calculator: press **[apps]**, select `Cesium`, highlight `CALCGPT`, press **[enter]**. The screen will print "Ready" once it's listening on USB.
5. Set your API key (skip this if `setup.bat` already saved it):
   ```powershell
   # Windows PowerShell
   $env:ANTHROPIC_API_KEY = "your-anthropic-api-key"
   ```
   ```bash
   # macOS / Linux
   export ANTHROPIC_API_KEY=your-anthropic-api-key
   ```
6. Run the host:
   ```
   run.bat
   ```
   or directly:
   ```
   python DesktopProgram/calcgpt.py
   ```
   It auto-detects the calculator on most systems; if not, pick the correct port from the list.
7. The calculator switches to "Connected!" once the host writes its handshake byte.
8. Press **2nd** to open the input screen. Type with the alpha keys. Press **ALPHA** to cycle between `[123]`, `[ABC]`, and `[abc]`. Press **DEL** to backspace, **MODE** to cancel, **enter** to send.
9. The reply streams back. Use **up/down arrows** to scroll older pages.
10. Send `/reset` (or `clear`) as a prompt to wipe the conversation.

---

## ⚙ Configuration

The constants at the top of `DesktopProgram/calcgpt.py` are the main knobs:

- `MODEL` - defaults to `claude-haiku-4-5` (fast and cheap, plenty for short calculator-screen replies). Swap to `claude-sonnet-4-6` or `claude-opus-4-7` for higher-quality replies at higher cost.
- `MAX_TOKENS` - caps the response length.
- `SYSTEM_PROMPT` - tells Claude to keep replies short, ASCII-only, and to use the `calculate` tool for any arithmetic.
- `TOOLS` - the tool definitions exposed to Claude. Add your own here.

---

## 📝 Notes

- Tested on a TI-84 Plus CE running TIOS 5.1. Other models / firmware may need changes.
- The calculator's serial buffer is 64 bytes, so the host writes at most 63 bytes per chunk with a small inter-chunk delay.
- Smart quotes, em-dashes, and other unicode coming back from Claude are folded to ASCII before being written to the calculator so they don't render as garbage.
- The `calculate` tool runs Python `eval` against a sandboxed namespace (no `__builtins__`, only `math` plus a few built-ins). It's not a full Python REPL, but it handles arithmetic, trig, logs, factorials, etc.
