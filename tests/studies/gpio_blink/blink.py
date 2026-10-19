#!/usr/bin/env python3
"""Slow GPIO blink on the IR TX pin (manual, hand-stoppable).

Flashes a library-free sketch to the TX board that toggles TEST_IR_TX_GPIO slowly,
so you can watch the IR LED through a phone camera and tell a wiring/drive problem
from a library problem. Each level change is read back from the pad; a mismatch
means the pin is not actually reaching the level it is driven to.

Usage (from tests/):
    uv run python studies/gpio_blink/blink.py                              # ESP32-S3 TX board
    uv run python studies/gpio_blink/blink.py --peer-profile tx:tx_esp32   # ESP32 classic
    uv run python studies/gpio_blink/blink.py --hold on                    # hold the LED on (multimeter)
    uv run python studies/gpio_blink/blink.py --drive 3                    # strongest pad drive

Stop with Ctrl-C; a summary is printed. Reads tests/.env for the port and GPIO.
"""
import argparse
import subprocess
import sys
import time
from pathlib import Path

import serial  # pyserial (already a test dependency)

TESTS = Path(__file__).resolve().parents[2]  # .../ESP32IRPulseKit/tests
SKETCH_DIR = Path(__file__).resolve().parent


def load_env() -> dict:
    env_path = TESTS / ".env"
    env = {}
    if env_path.exists():
        for raw in env_path.read_text().splitlines():
            line = raw.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, _, v = line.partition("=")
            env[k.strip()] = v.strip()
    return env


def flash(profile: str, port: str, defines: str):
    cmd = [
        "arduino-cli", "compile",
        "--profile", profile,
        "-u", "-p", port,
        "--build-property", defines,
        str(SKETCH_DIR),
    ]
    print(f"[flash] {SKETCH_DIR.name} ({profile}) -> {port}", flush=True)
    subprocess.run(cmd, check=True, cwd=str(TESTS))


class LineReader:
    def __init__(self, ser: serial.Serial):
        self.ser = ser
        self.buf = b""

    def readline(self, deadline: float):
        while time.monotonic() < deadline:
            if b"\n" in self.buf:
                line, _, self.buf = self.buf.partition(b"\n")
                return line.decode(errors="replace").strip()
            chunk = self.ser.read(self.ser.in_waiting or 1)
            if chunk:
                self.buf += chunk
        return None


def wait_ready(reader: LineReader, ser: serial.Serial, timeout: float):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        ser.write(b"READY\n")
        line = reader.readline(time.monotonic() + 1.0)
        if line and ("GPIO_READY" in line or "GPIO_ERROR" in line):
            return line
    return None


def command(reader: LineReader, ser: serial.Serial, cmd: str, expect: str):
    ser.write(f"{cmd}\n".encode())
    deadline = time.monotonic() + 3.0
    while True:
        line = reader.readline(deadline)
        if line is None:
            sys.exit(f"[error] no response to {cmd!r}")
        print(f"  {line}", flush=True)
        if line.startswith("GPIO_ERROR"):
            sys.exit(f"[error] {line}")
        if line.startswith(expect):
            return line


def parse_level(line: str):
    """'GPIO set=1 read=1 led=on' -> (set, read)."""
    fields = dict(f.split("=", 1) for f in line.split()[1:] if "=" in f)
    return int(fields["set"]), int(fields["read"])


def main():
    ap = argparse.ArgumentParser(description="Slow GPIO blink on the IR TX pin")
    ap.add_argument("--peer-profile", default="tx:tx_esp32s3", metavar="tx:PROFILE",
                    help="TX board profile (e.g. tx:tx_esp32); port from "
                         "TEST_SERIAL_PORT_PEER_TX_<PROFILE>")
    ap.add_argument("--no-flash", action="store_true", help="connect to an already-flashed board")
    ap.add_argument("--period", type=int, default=1000, help="toggle period in ms (50-10000)")
    ap.add_argument("--hold", choices=("on", "off"), help="hold the LED on/off instead of blinking")
    ap.add_argument("--drive", type=int, choices=range(4), help="pad drive strength 0-3")
    args = ap.parse_args()

    peer_name, _, profile = args.peer_profile.partition(":")
    if peer_name != "tx" or not profile:
        ap.error("--peer-profile must be tx:PROFILE")

    env = load_env()
    key = profile.upper().replace("-", "_")
    port = env.get(f"TEST_SERIAL_PORT_PEER_TX_{key}")
    if not port:
        sys.exit(f"[error] TEST_SERIAL_PORT_PEER_TX_{key} is not set in tests/.env")
    gpio = env.get("TEST_IR_TX_GPIO", "4")
    inverted = env.get("TEST_IR_TX_INVERTED", "0")

    if not args.no_flash:
        flash(profile, port, f'build.defines=-DIR_TX_GPIO="{gpio}" -DIR_TX_INVERTED="{inverted}"')

    with serial.Serial(port, 115200, timeout=0) as ser:
        reader = LineReader(ser)
        ready = wait_ready(reader, ser, timeout=15)
        if ready is None:
            sys.exit("[error] board did not report GPIO_READY")
        print(f"[ready] {ready}", flush=True)
        if ready.startswith("GPIO_ERROR"):
            sys.exit(f"[error] GPIO{gpio} cannot drive an output on this board")

        if args.drive is not None:
            command(reader, ser, f"DRIVE {args.drive}", "GPIO_DRIVE")
        if args.hold:
            command(reader, ser, args.hold.upper(), "GPIO set=")
            print(f"[hold] LED {args.hold} on GPIO{gpio}. Measure the pin; Ctrl-C to stop.", flush=True)
        else:
            command(reader, ser, f"BLINK {args.period}", "GPIO_BLINK")
            print(f"[blink] GPIO{gpio} every {args.period} ms. Watch the IR LED through a phone "
                  "camera (front cameras usually pass IR); Ctrl-C to stop.", flush=True)

        toggles = 0
        mismatches = 0
        try:
            while True:
                line = reader.readline(time.monotonic() + 0.5)
                if not line or not line.startswith("GPIO set="):
                    continue
                level_set, level_read = parse_level(line)
                toggles += 1
                note = "" if level_set == level_read else "   <-- pad does not reach the driven level"
                mismatches += level_set != level_read
                print(f"  {line}{note}", flush=True)
        except KeyboardInterrupt:
            pass

    print(f"\n[summary] level changes={toggles} readback mismatches={mismatches}")
    if toggles == 0:
        print("  no level changes observed (use --hold for a fixed level)")
    elif mismatches:
        print(f"  GPIO{gpio} does not reach the levels it drives: the load is holding the pin "
              "(DATA input draws too much current / short), or it is the wrong pin. "
              "Disconnect the module and run again to see whether the bare pin follows.")
    else:
        print(f"  GPIO{gpio} reaches every driven level. If the LED did not follow, check the "
              "module side: common GND, DATA wired to this pin, module VCC, and that the "
              "module's DATA input switches at 3.3 V logic.")


if __name__ == "__main__":
    main()
