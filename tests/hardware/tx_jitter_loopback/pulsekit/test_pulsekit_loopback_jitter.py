import os
import re
import statistics
import time
from collections import Counter

import pytest
from pexpect import EOF, TIMEOUT


# Single-board wired loopback (LOOPBACK_TX_GPIO -> LOOPBACK_RX_GPIO), carrier
# disabled. TX and the 1 us RMT RX run on the same board (same clock) with no
# IR LED / TSOP in the path, so the spread across repeats is pure transmit
# timing jitter of the library under test.
TX_IMPL = "ESP32IRPulseKit"
SEND_PROTOCOL = "NEC"
SEND_BITS = 0xCB3400FF
# A modest count is plenty; the per-frame timing is highly repeatable. Override
# with JITTER_FRAMES.
FRAMES = int(os.environ.get("JITTER_FRAMES", "50"))
# Small pause between frames so the serial/USB pipeline fully drains before the
# next long RX_JITTER line, minimizing corrupted (truncated) lines. Override
# with JITTER_GAP_MS.
INTER_FRAME_S = float(os.environ.get("JITTER_GAP_MS", "5")) / 1000.0


RX_JITTER = re.compile(
    rb"RX_JITTER seq=(?P<seq>\d+) len=(?P<len>\d+) us=(?P<us>[0-9,]+)\r?\n"
)


def wait_ready(dut):
    dut.write("READY\n")
    dut.expect(re.compile(rb"RX_READY impl=\S+ .*resolution_us=1"), timeout=20)


def capture_one(dut):
    try:
        match = dut.expect(RX_JITTER, timeout=10)
    except (EOF, TIMEOUT):
        return None
    return [int(x) for x in match.group("us").decode().split(",") if x]


def test_pulsekit_loopback_jitter(dut, record_property):
    wait_ready(dut)
    dut.write("PING\n")
    dut.expect_exact("PONG", timeout=5)

    frames = []
    for _ in range(FRAMES):
        dut.write(f"SEND {SEND_PROTOCOL} {SEND_BITS:x}\n")
        dut.expect_exact(f"TX_OK {SEND_PROTOCOL} {SEND_BITS:x}", timeout=5)
        cap = capture_one(dut)
        if cap:
            frames.append(cap)
        time.sleep(INTER_FRAME_S)

    if not frames:
        pytest.fail(f"{TX_IMPL}: no frames captured by the 1 us RMT RX.", pytrace=False)

    # A correct NEC frame always has the same edge count; only the durations
    # vary. Align on the most common length and drop corrupted/split captures.
    lengths = Counter(len(f) for f in frames)
    common_len, _ = lengths.most_common(1)[0]
    aligned = [f for f in frames if len(f) == common_len]

    per_edge_stdev = []
    per_edge_ptp = []
    for i in range(common_len):
        col = [f[i] for f in aligned]
        per_edge_stdev.append(statistics.pstdev(col) if len(col) > 1 else 0.0)
        per_edge_ptp.append(max(col) - min(col))

    mean_stdev = statistics.mean(per_edge_stdev) if per_edge_stdev else 0.0
    max_stdev = max(per_edge_stdev) if per_edge_stdev else 0.0
    max_ptp = max(per_edge_ptp) if per_edge_ptp else 0

    record_property("tx_impl", TX_IMPL)
    record_property("mode", "loopback-carrier-off")
    record_property("protocol", SEND_PROTOCOL)
    record_property("frames_captured", len(frames))
    record_property("frames_used", len(aligned))
    record_property("edges", common_len)
    record_property("mean_stdev_us", round(mean_stdev, 2))
    record_property("max_stdev_us", round(max_stdev, 2))
    record_property("max_peak_to_peak_us", max_ptp)
    print(
        f"JITTER_LOOPBACK_OBSERVED tx={TX_IMPL} proto={SEND_PROTOCOL} "
        f"frames_used={len(aligned)}/{len(frames)} edges={common_len} "
        f"mean_stdev_us={mean_stdev:.2f} max_stdev_us={max_stdev:.2f} "
        f"max_ptp_us={max_ptp}"
    )

    assert len(aligned) >= max(3, FRAMES // 2)
