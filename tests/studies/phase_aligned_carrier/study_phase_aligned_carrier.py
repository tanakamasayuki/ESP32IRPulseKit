import os
import re
import statistics
import time
from collections import Counter

import pytest
from pexpect import EOF, TIMEOUT


# A/B experiment: the peer (peer_tx/) sends a NEC-shaped frame through the
# library IRSender in two carrier modes and the 1 us probe (dut) measures how
# stable the demodulated bit-marks are across repeats:
#   - hw: free-running hardware carrier (rmt_apply_carrier), today's default
#   - pa: phase-aligned, symbol-encoded carrier (setPhaseAlignedCarrier(true))
# Hypothesis: pa removes the free-running ±1-cycle wobble, so per-mark jitter
# (standard deviation / peak-to-peak across frames) drops vs hw.
#
# Sweeps (override via env):
#   PA_MARKS="540,560,580"  PA_CARRIER_HZ="38000"  PA_DUTY="33"  PA_FRAMES="20"
MODES = ["hw", "pa"]
MARKS = [int(x) for x in os.environ.get("PA_MARKS", "540,560,580").split(",") if x]
CARRIER_HZ = int(os.environ.get("PA_CARRIER_HZ", "38000"))
DUTY = float(os.environ.get("PA_DUTY", "33"))
FRAMES = int(os.environ.get("PA_FRAMES", "20"))
CSV_PATH = os.environ.get("PA_CSV", "/tmp/phase_aligned_carrier.csv")


RX_JITTER = re.compile(
    rb"RX_JITTER seq=(?P<seq>\d+) len=(?P<len>\d+) us=(?P<us>[0-9,]+)\r?\n"
)
TX_OK = re.compile(
    rb"TX_OK mode=(?P<mode>hw|pa) mark=(?P<mark>\d+) hz=(?P<hz>\d+) duty=(?P<duty>[0-9.]+)"
)
TX_READY = re.compile(rb"TX_READY impl=ESP32IRPulseKit .*resolution_us=1")


def wait_boards_ready(dut, peers):
    tx = peers["tx"]
    rx = dut
    rx.write("READY\n")
    tx.write("READY\n")
    rx.expect(re.compile(rb"RX_READY impl=\S+ .*resolution_us=1"), timeout=20)
    tx.expect(TX_READY, timeout=20)
    return tx, rx


def capture_one(rx, timeout=10):
    try:
        match = rx.expect(RX_JITTER, timeout=timeout)
    except (EOF, TIMEOUT):
        return None
    return [int(x) for x in match.group("us").decode().split(",") if x]


def send_one(tx, mode, mark, hz, duty):
    """Send one frame, waiting for TX_OK. A mode switch re-creates the channel on
    the peer, so the first SEND of a new mode can take longer; retry on timeout
    and re-sync if the peer rebooted."""
    for _ in range(4):
        tx.write(f"SEND {mode} {mark} {hz} {duty:g}\n")
        try:
            tx.expect(TX_OK, timeout=6)
            return True
        except (EOF, TIMEOUT):
            try:
                tx.expect(TX_READY, timeout=8)
            except (EOF, TIMEOUT):
                pass
    return False


def collect_point(tx, rx, mode, mark, hz, duty, frames):
    if not send_one(tx, mode, mark, hz, duty):
        return []
    capture_one(rx)  # settle / absorb the mode-change transient
    caps = []
    for _ in range(frames):
        if not send_one(tx, mode, mark, hz, duty):
            break
        cap = capture_one(rx)
        if cap:
            caps.append(cap)
        time.sleep(0.005)
    if not caps:
        return []
    common_len, _ = Counter(len(c) for c in caps).most_common(1)[0]
    return [c for c in caps if len(c) == common_len]


def mark_jitter(aligned, mark):
    """Mean per-edge stdev and max peak-to-peak over the bit-mark edges (even
    indices whose mean is near the commanded width; excludes the 9000 header and
    all spaces)."""
    n = len(aligned[0])
    mean = [statistics.mean(c[i] for c in aligned) for i in range(n)]
    sd = [statistics.pstdev([c[i] for c in aligned]) for i in range(n)]
    ptp = [max(c[i] for c in aligned) - min(c[i] for c in aligned) for i in range(n)]
    idx = [i for i in range(0, n, 2) if abs(mean[i] - mark) < 200]
    if not idx:
        return None
    return {
        "mark_mean": statistics.mean(mean[i] for i in idx),
        "mark_sd": statistics.mean(sd[i] for i in idx),
        "mark_max_ptp": max(ptp[i] for i in idx),
    }


def test_phase_aligned_vs_hardware_carrier(dut, peers, record_property):
    tx, rx = wait_boards_ready(dut, peers)
    tx.write("PING\n")
    tx.expect_exact("PONG", timeout=5)
    rx.write("PING\n")
    rx.expect_exact("PONG", timeout=5)

    # Consume the probe's one-shot prime frame.
    send_one(tx, "hw", MARKS[0], CARRIER_HZ, DUTY)
    capture_one(rx, timeout=3)

    csv_file = None
    try:
        parent = os.path.dirname(CSV_PATH)
        if parent:
            os.makedirs(parent, exist_ok=True)
        csv_file = open(CSV_PATH, "w", encoding="utf-8")
        csv_file.write("mode,mark_us,carrier_hz,duty_pct,frames,"
                       "mark_mean_us,mark_sd_us,mark_max_ptp_us\n")
        csv_file.flush()
    except OSError as exc:
        print(f"PHASE_ALIGNED_CSV_ERROR {exc}")

    results = {}  # (mode, mark) -> stats
    for mark in MARKS:
        for mode in MODES:
            aligned = collect_point(tx, rx, mode, mark, CARRIER_HZ, DUTY, FRAMES)
            if len(aligned) < 3:
                print(f"PHASE_ALIGNED mode={mode} mark={mark} "
                      f"frames={len(aligned)} INSUFFICIENT")
                continue
            stats = mark_jitter(aligned, mark)
            if not stats:
                print(f"PHASE_ALIGNED mode={mode} mark={mark} NO_MARK_EDGES")
                continue
            results[(mode, mark)] = stats
            if csv_file:
                csv_file.write(
                    f"{mode},{mark},{CARRIER_HZ},{DUTY:g},{len(aligned)},"
                    f"{stats['mark_mean']:.1f},{stats['mark_sd']:.2f},"
                    f"{stats['mark_max_ptp']}\n")
                csv_file.flush()
            print(f"PHASE_ALIGNED mode={mode} mark={mark} frames={len(aligned)} "
                  f"mark_mean={stats['mark_mean']:.1f} mark_sd={stats['mark_sd']:.2f} "
                  f"mark_max_ptp={stats['mark_max_ptp']}")

    # Side-by-side hw vs pa per mark width.
    for mark in MARKS:
        hw = results.get(("hw", mark))
        pa = results.get(("pa", mark))
        if hw and pa:
            print(f"PHASE_ALIGNED_COMPARE mark={mark} "
                  f"hw_sd={hw['mark_sd']:.2f} pa_sd={pa['mark_sd']:.2f} "
                  f"hw_ptp={hw['mark_max_ptp']} pa_ptp={pa['mark_max_ptp']} "
                  f"hw_mean={hw['mark_mean']:.1f} pa_mean={pa['mark_mean']:.1f}")

    if csv_file:
        csv_file.close()
        print(f"PHASE_ALIGNED_CSV {CSV_PATH} points={len(results)}")

    record_property("modes", MODES)
    record_property("marks", MARKS)
    record_property("points", len(results))

    # Observation study: require only that we collected data for both modes so the
    # comparison is meaningful. Whether pa actually beats hw is read from the log.
    assert any(k[0] == "hw" for k in results), "no hardware-carrier points collected"
    assert any(k[0] == "pa" for k in results), "no phase-aligned points collected"
