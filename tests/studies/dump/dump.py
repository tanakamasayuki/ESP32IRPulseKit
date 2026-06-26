#!/usr/bin/env python3
"""Manual IR dump runner (build+flash, then stream).

This is NOT a pass/fail test. It just spares you from editing the GPIO in an
example sketch by hand: it reads tests/.env for the serial port and RX GPIO,
flashes the combined dump sketch (generic protocols + AC vendor decode + raw
replay, all in one), and prints every received frame to the console. Point a
remote at the receiver and read the dump. Stop with Ctrl-C.

Usage (from tests/):
    uv run python studies/dump/dump.py               # build+upload, then stream
    uv run python studies/dump/dump.py --no-flash    # connect to a flashed board
    uv run python studies/dump/dump.py --idle-us 35000   # snappier generic-only

Reads tests/.env for the port / GPIO (same keys as studies/link_quality):
    TEST_SERIAL_PORT_RX_ESP32S3, TEST_IR_RX_GPIO, TEST_IR_RX_INVERTED
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


def flash(sketch_dir: Path, profile: str, port: str, defines: str):
    cmd = [
        "arduino-cli", "compile",
        "--profile", profile,
        "-u", "-p", port,
        "--build-property", defines,
        str(sketch_dir),
    ]
    print(f"[flash] {sketch_dir.name} -> {port}", flush=True)
    subprocess.run(cmd, check=True, cwd=str(TESTS))


def main():
    ap = argparse.ArgumentParser(description="Manual IR dump (generic + AC)")
    ap.add_argument("--no-flash", action="store_true",
                    help="skip build+upload and connect to an already-flashed board")
    ap.add_argument("--profile", default="rx_esp32s3", help="arduino-cli sketch profile")
    ap.add_argument("--port", help="serial port (default: TEST_SERIAL_PORT_RX_ESP32S3 from .env)")
    ap.add_argument("--gpio", help="RX GPIO (default: TEST_IR_RX_GPIO from .env)")
    ap.add_argument("--inverted", help="RX inverted 0/1 (default: TEST_IR_RX_INVERTED from .env)")
    ap.add_argument("--idle-us", default="100000",
                    help="capture idle threshold in us; lower (e.g. 35000) for snappier "
                         "generic-only, raise if one AC press splits into several dumps")
    ap.add_argument("--max-symbols", default="1024", help="RX capture capacity (AC frames are long)")
    args = ap.parse_args()

    env = load_env()
    port = args.port or env.get("TEST_SERIAL_PORT_RX_ESP32S3", "/dev/ttyACM0")
    gpio = args.gpio or env.get("TEST_IR_RX_GPIO", "32")
    inverted = args.inverted or env.get("TEST_IR_RX_INVERTED", "1")

    if not args.no_flash:
        defines = (
            f'build.defines='
            f'-DIR_RX_GPIO="{gpio}" -DIR_RX_INVERTED="{inverted}" '
            f'-DIR_RX_IDLE_US="{args.idle_us}" -DIR_RX_MAX_SYMBOLS="{args.max_symbols}"'
        )
        flash(SKETCH_DIR, args.profile, port, defines)

    print(f"[open] {port}  gpio={gpio} inverted={inverted} "
          f"idle_us={args.idle_us} max_symbols={args.max_symbols}", flush=True)
    print("[run] point a remote at the receiver; Ctrl-C to stop.\n", flush=True)

    try:
        with serial.Serial(port, 115200, timeout=0.2) as ser:
            ser.write(b"READY\n")  # prompt the board to (re)print its DUMP_READY line
            try:
                while True:
                    chunk = ser.read(ser.in_waiting or 1)
                    if chunk:
                        sys.stdout.buffer.write(chunk)
                        sys.stdout.buffer.flush()
                    else:
                        time.sleep(0.01)
            except KeyboardInterrupt:
                print("\n\n[stopped]", flush=True)
    except serial.SerialException as exc:
        print(f"ERROR: cannot open {port}: {exc}", file=sys.stderr)
        print("Check the port in tests/.env (TEST_SERIAL_PORT_RX_ESP32S3) or pass --port.",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
