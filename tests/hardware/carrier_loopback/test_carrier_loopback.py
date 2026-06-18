import collections
import os
import re
import sys
import time

import pytest
from pexpect import EOF, TIMEOUT

sys.path.insert(0, os.path.dirname(__file__))
import analyze  # noqa: E402

# Single-board wired loopback (LOOPBACK_TX_GPIO -> LOOPBACK_RX_GPIO), carrier ON,
# captured at 1 us with the RMT peripheral and no TSOP in the path. This looks
# directly at the carrier the library emits: period stability (TX resolution)
# and mark-to-mark phase drift (free-running carrier).
MARK_US = int(os.environ.get("CL_MARK", "530"))
SPACE_US = int(os.environ.get("CL_SPACE", "530"))
COUNT = int(os.environ.get("CL_COUNT", "4"))
DUTY = os.environ.get("CL_DUTY", "33")
CARRIER_HZ = os.environ.get("CL_HZ", "38000")
SENDS = int(os.environ.get("CL_SENDS", "20"))
OUT = os.environ.get("CL_OUT", os.path.join(os.path.dirname(__file__), "data", "carrier_loopback.txt"))

CARRIER_RAW = re.compile(rb"CARRIER_RAW seq=\d+ lvl0=[01] len=\d+ us=[0-9,]+\r?\n")


def wait_ready(dut):
    dut.write("READY\n")
    dut.expect(re.compile(rb"RX_READY impl=\S+ .*carrier=on resolution_us=1"), timeout=20)


def capture_set(dut, mark_us):
    lines = []
    for _ in range(SENDS):
        dut.write(f"CAP {mark_us} {SPACE_US} {COUNT} {DUTY} {CARRIER_HZ}\n")
        try:
            dut.expect_exact("CAP_OK", timeout=5)
            m = dut.expect(CARRIER_RAW, timeout=8)
        except (EOF, TIMEOUT):
            continue
        lines.append(m.group(0).decode(errors="replace").strip())
        time.sleep(0.02)
    return lines


def test_carrier_loopback_probe(dut, record_property):
    wait_ready(dut)
    dut.write("PING\n")
    dut.expect_exact("PONG", timeout=5)

    # CL_MARKS (comma list) sweeps mark widths in one flash; defaults to CL_MARK.
    marks_list = [int(x) for x in os.environ.get("CL_MARKS", str(MARK_US)).split(",")]
    os.makedirs(os.path.dirname(OUT), exist_ok=True)

    last_summary = {}
    for mark_us in marks_list:
        lines = capture_set(dut, mark_us)
        text = "\n".join(lines)
        out = OUT if len(marks_list) == 1 else OUT.replace(".txt", f"_{mark_us}.txt")
        with open(out, "w") as fh:
            fh.write(text + "\n")

        frames, marks, summary = analyze.analyze(text)
        last_summary = summary
        hist = dict(sorted(collections.Counter(m["cycles"] for m in marks).items()))
        period = summary.get("period_mean", float("nan"))
        cyc_float = mark_us / period if period == period and period else float("nan")
        print(f"\nCARRIER_LOOPBACK mark={mark_us} space={SPACE_US} duty={DUTY} "
              f"hz={CARRIER_HZ} sends={SENDS}  (mark/period = {cyc_float:.2f} cycles)")
        analyze.print_summary(summary, marks)
        print(f"  cycles histogram: {hist}")
        record_property(f"hist_{mark_us}", str(hist))

    assert last_summary, "no carrier marks parsed (check loopback jumper LOOPBACK_TX->RX)"
    assert last_summary["marks"] >= COUNT, "too few marks captured"
    assert 15.0 <= last_summary["period_mean"] <= 40.0, (
        f"carrier period {last_summary['period_mean']:.1f}us out of range for {CARRIER_HZ} Hz")
