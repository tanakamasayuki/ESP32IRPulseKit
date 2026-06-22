#!/usr/bin/env python3
"""Live IR link-quality meter (manual, hand-stoppable).

Drives an ESP32IRPulseKit TX board to send a fixed NEC frame in a loop and
scores how the ESP32IRPulseKit RX board demodulates it, updating a single
console line in place. Move the boards around by hand and watch the score:
the verdict tells you whether you are TOO CLOSE (TSOP saturated: marks short /
spaces inflated), TOO FAR (weak / dropouts), UNSTABLE (jitter), or GOOD.

It is NOT a pass/fail test. Stop it with Ctrl-C; a summary is printed.

Usage (from tests/):
    uv run python studies/link_quality/monitor.py              # build+upload, then run
    uv run python studies/link_quality/monitor.py --no-flash   # connect to flashed boards

Flashing is the default so stale/mismatched firmware can't silently fool the
reading. The compat margin is scored against the strictest external receiver
(JVC on IRremoteESP8266, ~594us zero-space ceiling), so "GOOD" means every
built-in protocol should decode externally, not just NEC.

Reads tests/.env for ports / GPIOs.
"""
import argparse
import collections
import os
import statistics
import subprocess
import sys
import time
from pathlib import Path

import serial  # pyserial (already a test dependency)

# --- NEC nominal timings (us) and the tolerance edges we score against ------
NEC_MARK = 560
NEC_ZERO_SPACE = 560
NEC_ONE_SPACE = 1690
SPACE_SPLIT = (NEC_ZERO_SPACE + NEC_ONE_SPACE) / 2  # zero/one classifier ~1125us

# Our own decoder accepts zero-space up to 560*1.25 = 700us.
OUR_ZERO_CEIL = NEC_ZERO_SPACE * 1.25
# The meter transmits a NEC frame, so the measured zero-space has a 560us nominal.
SENT_ZERO_US = NEC_ZERO_SPACE
# Strictest external receiver: IRremoteESP8266 decoding *JVC*. It subtracts a
# 50us mark-excess from the desired space, so its zero-space window tops out at
# (nominal-50)*1.25. JVC's 525us nominal gives the tightest ceiling of all the
# protocols (594us vs NEC's 638us), so we score against JVC to be conservative.
STRICT_ZERO_NOMINAL = 525            # JVC zero-space nominal
STRICT_ZERO_CEIL = (525 - 50) * 1.25  # ~594us (JVC on IRremoteESP8266)
TICK_US = 10  # our RX dumps ticks in 10us units

REPO = Path(__file__).resolve().parents[3]  # .../ESP32IRPulseKit
TESTS = Path(__file__).resolve().parents[2]  # .../ESP32IRPulseKit/tests
RX_DIR = Path(__file__).resolve().parent / "rx"
TX_DIR = Path(__file__).resolve().parent / "tx"


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


# --- ANSI helpers -----------------------------------------------------------
COLORS = {"GOOD": "\033[32m", "CLOSE": "\033[33m", "BAD": "\033[31m", "0": "\033[0m"}


def colorize(verdict: str, enabled: bool) -> str:
    if not enabled:
        return verdict
    if verdict.startswith("GOOD"):
        c = COLORS["GOOD"]
    elif "TOO CLOSE" in verdict or "MARGINAL" in verdict or "CLOSE" in verdict:
        c = COLORS["CLOSE"]
    else:
        c = COLORS["BAD"]
    return f"{c}{verdict}{COLORS['0']}"


# --- arduino-cli build/upload -----------------------------------------------
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


# --- buffered line reader ---------------------------------------------------
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

    def flush_input(self):
        self.ser.reset_input_buffer()
        self.buf = b""


def wait_ready(reader: LineReader, ser: serial.Serial, token: str, timeout: float):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        ser.write(b"READY\n")
        line = reader.readline(time.monotonic() + 1.0)
        if line and token in line:
            return line
    return None


# --- per-frame parsing ------------------------------------------------------
def parse_rx(line: str):
    """Return (decoded, bits, raw_len, marks_us, zero_spaces_us, one_spaces_us)."""
    if "ticks=" not in line:
        return None
    decoded = line.startswith("RX_DECODE")
    bits = None
    if decoded:
        for tok in line.split():
            if tok.startswith("bits=0x"):
                try:
                    bits = int(tok[len("bits=0x"):], 16)
                except ValueError:
                    bits = None
    ticks_str = line.split("ticks=", 1)[1].strip()
    try:
        ticks = [int(x) * TICK_US for x in ticks_str.split(",") if x != ""]
    except ValueError:
        return None
    raw_len = len(ticks)
    # header = ticks[0:2]; bits start at index 2 as (mark, space) pairs.
    marks, zeros, ones = [], [], []
    i = 2
    while i + 1 < len(ticks) and len(marks) < 32:
        mark, space = ticks[i], ticks[i + 1]
        marks.append(mark)
        (ones if space >= SPACE_SPLIT else zeros).append(space)
        i += 2
    return decoded, bits, raw_len, marks, zeros, ones


def pct(values, p):
    if not values:
        return None
    s = sorted(values)
    k = max(0, min(len(s) - 1, int(round((p / 100.0) * (len(s) - 1)))))
    return s[k]


def main():
    ap = argparse.ArgumentParser(description="Live IR link-quality meter")
    ap.add_argument("--no-flash", action="store_true",
                    help="skip build+upload and connect to already-flashed boards "
                         "(default: always flash, so stale/mismatched firmware can't fool you)")
    ap.add_argument("--window", type=int, default=30, help="rolling window (frames)")
    ap.add_argument("--interval", type=float, default=0.2, help="seconds between sends")
    ap.add_argument("--bits", default="cb3400ff", help="NEC payload (hex)")
    ap.add_argument("--no-color", action="store_true")
    args = ap.parse_args()

    env = load_env()
    rx_port = env.get("TEST_SERIAL_PORT_RX_ESP32S3", "/dev/ttyUSB1")
    tx_port = env.get("TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3", "/dev/ttyUSB0")
    rx_gpio = env.get("TEST_IR_RX_GPIO", "4")
    rx_inv = env.get("TEST_IR_RX_INVERTED", "1")
    tx_gpio = env.get("TEST_IR_TX_GPIO", "4")
    tx_inv = env.get("TEST_IR_TX_INVERTED", "0")
    color = not args.no_color and sys.stdout.isatty()

    if not args.no_flash:
        flash(RX_DIR, "rx_esp32s3", rx_port,
              f'build.defines=-DIR_RX_GPIO="{rx_gpio}" -DIR_RX_INVERTED="{rx_inv}"')
        flash(TX_DIR, "tx_esp32s3", tx_port,
              f'build.defines=-DIR_TX_GPIO="{tx_gpio}" -DIR_TX_INVERTED="{tx_inv}"')

    expect_bits = int(args.bits, 16)
    print(f"[open] RX={rx_port} TX={tx_port}  payload=NEC 0x{args.bits}", flush=True)
    with serial.Serial(rx_port, 115200, timeout=0) as rx, \
            serial.Serial(tx_port, 115200, timeout=0) as tx:
        rx_reader, tx_reader = LineReader(rx), LineReader(tx)
        print("[wait] boards booting (~5s)...", flush=True)
        if not wait_ready(rx_reader, rx, "RX_READY", 20):
            print("ERROR: RX board not ready (check port/flash).")
            return 1
        if not wait_ready(tx_reader, tx, "TX_READY", 20):
            print("ERROR: TX board not ready (check port/flash).")
            return 1
        print("[run] move the boards by hand; Ctrl-C to stop.\n", flush=True)

        window = collections.deque(maxlen=args.window)
        cmd = f"SEND NEC {args.bits}\n".encode()
        try:
            while True:
                t0 = time.monotonic()
                rx_reader.flush_input()
                tx.write(cmd)
                # one frame ~67ms; allow margin for serial + processing
                line = rx_reader.readline(time.monotonic() + 0.35)
                rec = parse_rx(line) if line else None
                window.append(rec)
                render(window, expect_bits, color)
                dt = args.interval - (time.monotonic() - t0)
                if dt > 0:
                    time.sleep(dt)
        except KeyboardInterrupt:
            print("\n\n[stopped]")
            summary(window, expect_bits)
    return 0


def aggregate(window, expect_bits):
    n = len(window)
    recv = sum(1 for r in window if r is not None)
    decoded = sum(1 for r in window if r and r[0])
    matched = sum(1 for r in window if r and r[0] and r[1] == expect_bits)
    marks, zeros, ones = [], [], []
    for r in window:
        if r:
            marks += r[3]
            zeros += r[4]
            ones += r[5]
    return n, recv, decoded, matched, marks, zeros, ones


def score_and_verdict(n, recv, decoded, marks, zeros, ones):
    recv_rate = recv / n if n else 0.0
    decode_rate = decoded / n if n else 0.0
    mark_mean = statistics.fmean(marks) if marks else float("nan")
    mark_sd = statistics.pstdev(marks) if len(marks) > 1 else 0.0
    sp0_mean = statistics.fmean(zeros) if zeros else float("nan")
    sp0_sd = statistics.pstdev(zeros) if len(zeros) > 1 else 0.0
    sp1_mean = statistics.fmean(ones) if ones else float("nan")
    sp0_p90 = pct(zeros, 90)

    mark_bias = (mark_mean - NEC_MARK) if marks else 0.0
    # The demod inflates the zero-space by ~the mark deficit (a roughly fixed
    # absolute surplus, independent of the nominal). We measure that surplus from
    # the transmitted NEC frame, then project it onto the *strictest* protocol
    # (JVC) to report worst-case headroom before an external RX rejects it.
    inflation = (sp0_p90 - SENT_ZERO_US) if sp0_p90 is not None else float("nan")
    compat_margin = (STRICT_ZERO_CEIL - (STRICT_ZERO_NOMINAL + inflation)
                     if sp0_p90 is not None else float("nan"))
    jitter = statistics.fmean([mark_sd, sp0_sd]) if marks else 0.0

    def clamp(x):
        return max(0.0, min(1.0, x))

    m_term = clamp(compat_margin / 60.0) if zeros else 0.0
    j_term = clamp(1.0 - jitter / 40.0) if marks else 0.0
    score = round(60 * decode_rate + 25 * m_term + 15 * j_term)

    saturated = (marks and mark_bias < -20 and sp0_mean > 620)
    if recv_rate < 0.5:
        verdict = "TOO FAR / WEAK (dropouts)"
    elif decode_rate < 0.5:
        verdict = "TOO CLOSE (saturated)" if saturated else "TOO FAR / WEAK"
    elif zeros and compat_margin < 0:
        verdict = "MARGINAL (external RX may reject)"
    elif jitter > 35:
        verdict = "UNSTABLE (jitter)"
    elif saturated:
        verdict = "GOOD-ish (close, watch margin)"
    else:
        verdict = "GOOD"

    return dict(recv_rate=recv_rate, decode_rate=decode_rate, mark_mean=mark_mean,
                mark_bias=mark_bias, mark_sd=mark_sd, sp0_mean=sp0_mean, sp0_sd=sp0_sd,
                sp1_mean=sp1_mean, compat_margin=compat_margin, jitter=jitter,
                score=score, verdict=verdict)


def render(window, expect_bits, color):
    n, recv, decoded, matched, marks, zeros, ones = aggregate(window, expect_bits)
    s = score_and_verdict(n, recv, decoded, marks, zeros, ones)

    def f(x, w=0):
        return f"{x:{w}.0f}" if x == x else "  --"  # NaN -> --

    line = (
        f"[{colorize(s['verdict'], color):<{38 if color else 30}}] "
        f"decode {decoded*100//n if n else 0:3d}%({decoded}/{n})  "
        f"recv {recv*100//n if n else 0:3d}%  "
        f"mark {f(s['mark_mean'])}({s['mark_bias']:+.0f})sd{f(s['mark_sd'])}  "
        f"sp0 {f(s['sp0_mean'])}({s['sp0_mean']-NEC_ZERO_SPACE:+.0f})sd{f(s['sp0_sd'])}  "
        f"sp1 {f(s['sp1_mean'])}  "
        f"compat-margin(JVC) {s['compat_margin']:+.0f}us  "
        f"score {s['score']:3d}"
    )
    # pad to clear leftovers, update in place
    sys.stdout.write("\r\033[K" + line)
    sys.stdout.flush()


def summary(window, expect_bits):
    n, recv, decoded, matched, marks, zeros, ones = aggregate(window, expect_bits)
    s = score_and_verdict(n, recv, decoded, marks, zeros, ones)
    print(f"Final window: {n} frames")
    print(f"  verdict        : {s['verdict']}   score {s['score']}/100")
    print(f"  receive rate   : {recv}/{n}")
    print(f"  decode rate    : {decoded}/{n}  (bits match {matched}/{n})")
    print(f"  mark           : {s['mark_mean']:.0f}us ({s['mark_bias']:+.0f}) sd {s['mark_sd']:.0f}")
    print(f"  zero-space     : {s['sp0_mean']:.0f}us ({s['sp0_mean']-NEC_ZERO_SPACE:+.0f}) sd {s['sp0_sd']:.0f}")
    print(f"  one-space      : {s['sp1_mean']:.0f}us")
    print(f"  compat margin  : {s['compat_margin']:+.0f}us  (strictest: JVC on IRremoteESP8266, zero ceiling {STRICT_ZERO_CEIL:.0f}us)")


if __name__ == "__main__":
    sys.exit(main())
