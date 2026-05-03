"""calcgpt - bridge between a TI-84 Plus CE and Anthropic's Claude API.

Reads prompts from the calculator over USB serial, hands them to Claude with
a `calculate` tool for real arithmetic, and streams the reply back in 63-byte
chunks (the calculator's serial buffer is 64 bytes). Logs every turn to a
JSONL file. Set ANTHROPIC_API_KEY in your environment before running.
"""

import datetime
import json
import math
import os
import sys
import time

import serial
import serial.tools.list_ports
from anthropic import Anthropic

MODEL = "claude-haiku-4-5"
MAX_TOKENS = 512
CHUNK_SIZE = 63
BAUD = 9600
INTER_CHUNK_DELAY = 0.03

# TI-84 Plus CE USB identifiers (DirectUSB serial / TI Connect CE).
TI_USB_VID = 0x0451
TI_USB_PIDS = (0xE008, 0xE003, 0xE004, 0xE001)

SYSTEM_PROMPT = (
    "You are a helpful general-purpose assistant. Answer ANY question the "
    "user asks - math, science, history, trivia, current events, coding, "
    "casual chat, opinions, anything. Do NOT refuse or redirect based on "
    "the device the user is on; treat this like any other chat interface.\n"
    "\n"
    "Display: the user reads your replies on a small screen that only "
    "renders plain ASCII. Keep replies short - aim for under ~300 "
    "characters when you can. No markdown, no smart quotes, no em-dashes, "
    "no unicode.\n"
    "\n"
    "You have a `calculate` tool that evaluates math expressions in Python. "
    "Use it whenever the user asks for an actual numeric result so the "
    "answer is exact. Don't call it for non-math questions."
)

TOOLS = [
    {
        "name": "calculate",
        "description": (
            "Evaluate a math expression in Python and return the result. "
            "Supports +, -, *, /, **, %, parentheses, and the math module: "
            "sqrt, sin, cos, tan, asin, acos, atan, log, log2, log10, exp, "
            "pi, e, factorial, floor, ceil, etc. Also: abs, round, min, "
            "max, sum, pow."
        ),
        "input_schema": {
            "type": "object",
            "properties": {
                "expression": {
                    "type": "string",
                    "description": "Python math expression, e.g. 'sqrt(2) * 3' or 'sin(pi/4)'",
                },
            },
            "required": ["expression"],
        },
    }
]

_SAFE_NS = {k: getattr(math, k) for k in dir(math) if not k.startswith("_")}
_SAFE_NS.update({
    "abs": abs, "round": round, "min": min, "max": max,
    "sum": sum, "pow": pow,
})


def calculate(expression):
    try:
        result = eval(expression, {"__builtins__": {}}, _SAFE_NS)
    except Exception as e:
        return f"error: {e}"
    if isinstance(result, float):
        if math.isfinite(result) and result.is_integer():
            return str(int(result))
        return f"{result:.10g}"
    return str(result)


def run_tool(name, args):
    if name == "calculate":
        return calculate(args.get("expression", ""))
    return f"unknown tool: {name}"


_PUNCT_MAP = str.maketrans({
    "‘": "'", "’": "'",
    "“": '"', "”": '"',
    "–": "-", "—": "-",
    "…": "...",
})


def to_calc_ascii(s):
    """Strip text down to what the calculator screen can render."""
    return s.translate(_PUNCT_MAP).encode("ascii", "replace").decode("ascii")


def list_ports():
    return [p.device for p in serial.tools.list_ports.comports()]


def find_ti_port():
    """Return the first port matching a known TI USB VID/PID, else None."""
    for p in serial.tools.list_ports.comports():
        if p.vid == TI_USB_VID and p.pid in TI_USB_PIDS:
            return p.device
    return None


def pick_port():
    auto = find_ti_port()
    if auto:
        print(f"Auto-detected TI calculator on {auto}.")
        return auto
    ports = list_ports()
    if not ports:
        print("No serial ports found. Plug in the calculator and start calcGPT on it.")
        return None
    print("No TI device auto-detected. Available ports:")
    for i, p in enumerate(ports):
        print(f"  {i}: {p}")
    while True:
        choice = input("Enter selection (or blank to retry auto-detect): ").strip()
        if not choice:
            return None
        if choice.isdigit() and 0 <= int(choice) < len(ports):
            return ports[int(choice)]
        print("Invalid selection.")


def open_serial(port):
    ser = serial.Serial(port, BAUD, timeout=None)
    ser.write(b"ack")
    return ser


def write_chunked(ser, text):
    data = to_calc_ascii(text)
    for i in range(0, len(data), CHUNK_SIZE):
        ser.write(data[i:i + CHUNK_SIZE].encode("ascii"))
        time.sleep(INTER_CHUNK_DELAY)


class ChunkedWriter:
    """Buffer streamed text and flush in 63-byte chunks to serial."""

    def __init__(self, ser):
        self.ser = ser
        self.pending = ""

    def write(self, text):
        self.pending += to_calc_ascii(text)
        while len(self.pending) >= CHUNK_SIZE:
            self.ser.write(self.pending[:CHUNK_SIZE].encode("ascii"))
            self.pending = self.pending[CHUNK_SIZE:]
            time.sleep(INTER_CHUNK_DELAY)

    def flush(self):
        if self.pending:
            self.ser.write(self.pending.encode("ascii"))
            self.pending = ""


def open_log():
    path = f"calcgpt-log-{datetime.date.today().isoformat()}.jsonl"
    return open(path, "a", encoding="utf-8")


def log_event(log_file, event):
    log_file.write(json.dumps(event, default=str) + "\n")
    log_file.flush()


def block_for_input(block):
    """Convert a Claude SDK content block back into the dict the API accepts as
    input on a follow-up turn. model_dump() includes output-only fields like
    `parsed_output` that the API rejects with a 400, so build the dict by hand.
    """
    if block.type == "text":
        return {"type": "text", "text": block.text}
    if block.type == "tool_use":
        return {"type": "tool_use", "id": block.id, "name": block.name, "input": block.input}
    if block.type == "thinking":
        d = {"type": "thinking", "thinking": block.thinking}
        sig = getattr(block, "signature", None)
        if sig:
            d["signature"] = sig
        return d
    # Unknown block type: fall back to dump and best-effort strip.
    d = block.model_dump(exclude_none=True)
    d.pop("parsed_output", None)
    return d


def turn(client, ser, messages, log_file):
    """Run one user turn. May call tools recursively. Streams text to calc."""
    writer = ChunkedWriter(ser)
    final_text_parts = []
    while True:
        with client.messages.stream(
            model=MODEL,
            max_tokens=MAX_TOKENS,
            system=[{
                "type": "text",
                "text": SYSTEM_PROMPT,
                "cache_control": {"type": "ephemeral"},
            }],
            tools=TOOLS,
            messages=messages,
        ) as stream:
            for delta in stream.text_stream:
                writer.write(delta)
            final = stream.get_final_message()

        assistant_blocks_input = [block_for_input(b) for b in final.content]
        messages.append({"role": "assistant", "content": assistant_blocks_input})

        for block in assistant_blocks_input:
            if block.get("type") == "text":
                final_text_parts.append(block.get("text", ""))

        log_event(log_file, {
            "kind": "assistant",
            "content": [b.model_dump() for b in final.content],
            "stop_reason": final.stop_reason,
            "usage": final.usage.model_dump() if final.usage else None,
        })

        if final.stop_reason != "tool_use":
            writer.flush()
            return "".join(final_text_parts)

        # Run tool calls and feed results back in.
        tool_results = []
        for block in final.content:
            if block.type == "tool_use":
                print(f"  [tool {block.name}({block.input})]")
                result = run_tool(block.name, block.input)
                print(f"  [result {result}]")
                tool_results.append({
                    "type": "tool_result",
                    "tool_use_id": block.id,
                    "content": result,
                })
        messages.append({"role": "user", "content": tool_results})
        log_event(log_file, {"kind": "tool_results", "content": tool_results})


def run_loop(client, ser, messages, log_file):
    while True:
        raw = ser.readline()
        if not raw:
            continue
        prompt = raw.decode("utf-8", errors="replace").strip()
        if not prompt:
            continue

        print(f"\n> {prompt}")
        log_event(log_file, {"kind": "user", "content": prompt})

        if prompt.lower() in ("/reset", "/clear", "reset", "clear"):
            messages.clear()
            write_chunked(ser, "Conversation reset.")
            print("(history cleared)")
            log_event(log_file, {"kind": "system", "event": "history_cleared"})
            continue

        messages.append({"role": "user", "content": prompt})
        try:
            reply = turn(client, ser, messages, log_file)
        except (OSError, serial.SerialException):
            raise  # propagate so the outer loop can reconnect
        except Exception as e:
            err = f"[error: {e}]"
            print(err)
            try:
                write_chunked(ser, err[:200])
            except Exception:
                pass
            messages.pop()  # don't keep the failed turn in history
            log_event(log_file, {"kind": "error", "content": str(e)})
            continue
        print(f"Claude: {reply}")


def main():
    api_key = os.environ.get("ANTHROPIC_API_KEY")
    if not api_key:
        sys.exit("Set ANTHROPIC_API_KEY in your environment, then re-run this script.")
    client = Anthropic(api_key=api_key)
    log_file = open_log()
    print(f"Logging to {log_file.name}")
    print(f"Using model: {MODEL}")

    messages = []
    try:
        while True:
            port = pick_port()
            if port is None:
                print("Retrying port detection in 3s. Ctrl+C to quit.")
                time.sleep(3)
                continue
            try:
                ser = open_serial(port)
            except (OSError, serial.SerialException) as e:
                print(f"Couldn't open {port}: {e}. Retrying in 3s.")
                time.sleep(3)
                continue
            print(f"Connected on {port}. Press 2nd on the calculator to prompt.")
            try:
                run_loop(client, ser, messages, log_file)
            except (OSError, serial.SerialException) as e:
                print(f"\nSerial error: {e}. Trying to reconnect...")
                try: ser.close()
                except Exception: pass
                time.sleep(2)
                continue
    except KeyboardInterrupt:
        print("\nExiting.")
    finally:
        try:
            log_file.close()
        except Exception:
            pass


if __name__ == "__main__":
    main()
