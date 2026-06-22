import os
import re
import statistics

import pytest
from pexpect import EOF, TIMEOUT

# 2D sweep of JVC bit-mark x zero-space, transmitted by the PulseKit peer and
# received by IRremoteESP8266. For each (mark, zspace) cell we send N frames and
# record: how often it decodes as JVC, and the received zero-space distribution
# (so we can see the margin against IRremoteESP8266's FIXED ~595 us JVC zero
# ceiling = (526-50)*1.25, independent of what we transmit). One-space is held at
# the JVC standard; it is not the failing axis.
MARKS = [int(x) for x in os.environ.get("JS_MARKS", "500,510,520,530,540,550,560").split(",")]
ZSPACES = [int(x) for x in os.environ.get("JS_ZSPACES", "470,490,510,530,550").split(",")]
ONE_US = int(os.environ.get("JS_ONE", "1575"))
N = int(os.environ.get("JS_N", "12"))
BITS = os.environ.get("JS_BITS", "c0de")
CSV = os.environ.get("JS_CSV", os.path.join(os.path.dirname(__file__), "data", "jvc_sweep.csv"))

ZERO_CEIL = 595.0          # IRremoteESP8266 JVC zero-space ceiling, fixed
SPACE_SPLIT = 1100         # below this a received space is a zero bit
EXPECT_RAWLEN = 36         # 1 gap + 2 header + 32 + 1 trailer

RX = re.compile(
    rb"RX proto=(?P<proto>\S+) bits=0x(?P<bits>[0-9A-Fa-f]+) len=(?P<len>\d+) "
    rb"raw_len=(?P<raw_len>\d+) us=(?P<us>[0-9,]+)"
)


def wait_boards_ready(dut, peers):
    tx, rx = peers["tx"], dut
    rx.write("READY\n")
    tx.write("READY\n")
    rx.expect(re.compile(rb"RX_READY impl=\S+"), timeout=20)
    tx.expect(re.compile(rb"TX_READY impl=\S+"), timeout=20)
    return tx, rx


def read_frame(rx, timeout=8):
    """Return (proto, zero_spaces[list]) or None on timeout."""
    try:
        m = rx.expect(RX, timeout=timeout)
    except (EOF, TIMEOUT):
        return None
    proto = m.group("proto").decode()
    us = [int(x) for x in m.group("us").split(b",") if x != b""]
    zspaces = []
    if len(us) >= EXPECT_RAWLEN - 1:
        spaces = us[4:36:2]  # the 16 bit-spaces
        zspaces = [s for s in spaces if s < SPACE_SPLIT]
    return proto, zspaces


def pct(values, p):
    if not values:
        return float("nan")
    s = sorted(values)
    k = max(0, min(len(s) - 1, int(round((p / 100.0) * (len(s) - 1)))))
    return s[k]


def test_jvc_timing_sweep(dut, peers, record_property):
    tx, rx = wait_boards_ready(dut, peers)
    tx.write("PING\n"); tx.expect_exact("PONG", timeout=5)
    rx.write("PING\n"); rx.expect_exact("PONG", timeout=5)

    os.makedirs(os.path.dirname(CSV), exist_ok=True)
    fh = open(CSV, "w")
    fh.write("mark_us,zspace_us,one_us,n,decoded,pass_ratio,zero_n,zero_p90_us,zero_max_us,margin_p90_us\n")
    fh.flush()

    print(f"\nJVC_SWEEP marks={MARKS} zspaces={ZSPACES} one={ONE_US} N={N} bits=0x{BITS}")
    best = None
    for mark in MARKS:
        for zsp in ZSPACES:
            decoded = 0
            zeros = []
            for _ in range(N):
                tx.write(f"JVCRAW {mark} {zsp} {ONE_US} {BITS}\n")
                try:
                    tx.expect_exact("JVCRAW_OK", timeout=5)
                except (EOF, TIMEOUT):
                    continue
                fr = read_frame(rx)
                if fr is None:
                    continue
                proto, zspaces = fr
                if proto == "JVC":
                    decoded += 1
                zeros.extend(zspaces)
            ratio = decoded / N
            p90 = pct(zeros, 90)
            zmax = max(zeros) if zeros else float("nan")
            margin = (ZERO_CEIL - p90) if p90 == p90 else float("nan")
            fh.write(f"{mark},{zsp},{ONE_US},{N},{decoded},{ratio:.3f},{len(zeros)},"
                     f"{p90:.0f},{zmax:.0f},{margin:.0f}\n")
            fh.flush()
            print(f"  mark={mark} zsp={zsp}: pass {decoded}/{N}  zero p90={p90:.0f}us "
                  f"max={zmax:.0f}  margin={margin:+.0f}us")
            cand = (ratio, margin if margin == margin else -999, -(mark + zsp))
            if best is None or cand > best[0]:
                best = (cand, mark, zsp, ratio, margin)
    fh.close()

    if best:
        _, bm, bz, br, bmar = best
        print(f"  BEST: mark={bm} zsp={bz} pass_ratio={br:.2f} margin={bmar:+.0f}us")
        record_property("best_mark_us", bm)
        record_property("best_zspace_us", bz)
        record_property("best_pass_ratio", round(br, 3))
        record_property("best_margin_us", round(bmar, 1))

    assert best is not None, "no cells captured (check 2-board IR wiring TX->RX)"
