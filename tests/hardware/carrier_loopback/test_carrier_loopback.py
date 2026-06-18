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


def test_carrier_loopback_probe(dut, record_property):
    wait_ready(dut)
    dut.write("PING\n")
    dut.expect_exact("PONG", timeout=5)

    lines = []
    for _ in range(SENDS):
        dut.write(f"CAP {MARK_US} {SPACE_US} {COUNT} {DUTY} {CARRIER_HZ}\n")
        try:
            dut.expect_exact("CAP_OK", timeout=5)
            m = dut.expect(CARRIER_RAW, timeout=8)
        except (EOF, TIMEOUT):
            continue
        lines.append(m.group(0).decode(errors="replace").strip())
        time.sleep(0.02)

    text = "\n".join(lines)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as fh:
        fh.write(text + "\n")

    frames, marks, summary = analyze.analyze(text)
    print(f"\nCARRIER_LOOPBACK mark={MARK_US} space={SPACE_US} count={COUNT} "
          f"duty={DUTY} hz={CARRIER_HZ} sends={SENDS}")
    analyze.print_summary(summary, marks)

    record_property("marks", summary.get("marks", 0))
    record_property("period_mean_us", round(summary.get("period_mean", 0), 2))
    record_property("cycles_per_mark", str(summary.get("cycles_per_mark", [])))
    record_property("on_span_sd_us", round(summary.get("on_span_sd", 0), 2))

    # This is an observation rig: only sanity-check that we captured carrier.
    assert summary, "no carrier marks parsed (check loopback jumper LOOPBACK_TX->RX)"
    assert summary["marks"] >= COUNT, "too few marks captured"
    assert 15.0 <= summary["period_mean"] <= 40.0, (
        f"carrier period {summary['period_mean']:.1f}us out of plausible range "
        f"for {CARRIER_HZ} Hz")
