import os
import re
import statistics
import time
from collections import Counter

import pytest
from pexpect import EOF, TIMEOUT


# The peer (peer_tx/) transmits a NEC-shaped frame via raw RMT + hardware carrier,
# sweeping the bit-mark WIDTH, the carrier FREQUENCY, and the carrier DUTY. The
# dut (probe.ino) captures at 1 us and we measure how stable the demodulated mark
# edges are across repeats. Hypothesis: jitter dips when the mark width is an
# integer number of carrier periods (e.g. ~552.6 us = 21 * (1e6/38000)) and rises
# when it ends mid-cycle (560 us = 21.28 periods).

# Sweeps (override via env for coarse-then-fine runs):
#   CARRIER_HZ="38000"  CARRIER_DUTY="20,33,50"
#   CARRIER_MARKS="540,545,550,553,555,560,565,570,575,580"
#   CARRIER_FRAMES="20"
CARRIERS = [int(x) for x in os.environ.get("CARRIER_HZ", "38000").split(",") if x]
DUTIES = [float(x) for x in os.environ.get("CARRIER_DUTY", "20,33,50").split(",") if x]
MARKS = [int(x) for x in os.environ.get(
    "CARRIER_MARKS", "540,545,550,553,555,560,565,570,575,580").split(",") if x]
FRAMES = int(os.environ.get("CARRIER_FRAMES", "20"))
CSV_PATH = os.environ.get("CARRIER_CSV", "/tmp/carrier_jitter.csv")


RX_JITTER = re.compile(
    rb"RX_JITTER seq=(?P<seq>\d+) len=(?P<len>\d+) us=(?P<us>[0-9,]+)\r?\n"
)
TX_OK = re.compile(rb"TX_OK mark=(?P<mark>\d+) hz=(?P<hz>\d+) duty=(?P<duty>[0-9.]+)")


def wait_boards_ready(dut, peers):
    tx = peers["tx"]
    rx = dut
    rx.write("READY\n")
    tx.write("READY\n")
    rx.expect(re.compile(rb"RX_READY impl=\S+ .*resolution_us=1"), timeout=20)
    tx.expect(re.compile(rb"TX_READY impl=\S+ .*resolution_us=1"), timeout=20)
    return tx, rx


def capture_one(rx, timeout=10):
    try:
        match = rx.expect(RX_JITTER, timeout=timeout)
    except (EOF, TIMEOUT):
        return None
    return [int(x) for x in match.group("us").decode().split(",") if x]


TX_READY = re.compile(rb"TX_READY .*resolution_us=1")


def send_one(tx, mark, hz, duty):
    """Send one frame and wait for the peer's TX_OK. The peer can reset mid-run
    (e.g. brownout at high duty); on a timeout, wait for it to reboot and retry,
    so a single reset does not abort the whole sweep. Returns False if it never
    recovers."""
    for _ in range(4):
        tx.write(f"SEND {mark} {hz} {duty:g}\n")
        try:
            tx.expect(TX_OK, timeout=5)
            return True
        except (EOF, TIMEOUT):
            try:
                tx.expect(TX_READY, timeout=8)  # peer rebooted -> re-sync
            except (EOF, TIMEOUT):
                pass
    return False


def collect_point(tx, rx, mark, hz, duty, frames):
    """Send `frames`+1 frames (first discarded as a settle frame) and return the
    captured edge lists that match the modal length."""
    if not send_one(tx, mark, hz, duty):
        return []
    capture_one(rx)  # settle / absorb any carrier-change transient
    caps = []
    for _ in range(frames):
        if not send_one(tx, mark, hz, duty):
            break
        cap = capture_one(rx)
        if cap:
            caps.append(cap)
        time.sleep(0.005)
    if not caps:
        return []
    common_len, _ = Counter(len(c) for c in caps).most_common(1)[0]
    return [c for c in caps if len(c) == common_len]


def per_edge_stats(aligned):
    n = len(aligned[0])
    sd = []
    ptp = []
    for i in range(n):
        col = [c[i] for c in aligned]
        sd.append(statistics.pstdev(col) if len(col) > 1 else 0.0)
        ptp.append(max(col) - min(col))
    return sd, ptp


def test_carrier_jitter_sweep(dut, peers, record_property):
    tx, rx = wait_boards_ready(dut, peers)
    tx.write("PING\n")
    tx.expect_exact("PONG", timeout=5)
    rx.write("PING\n")
    rx.expect_exact("PONG", timeout=5)

    # Consume the RX one-shot prime (its first emitted frame is suppressed).
    send_one(tx, 560, CARRIERS[0], DUTIES[0])
    capture_one(rx, timeout=3)

    # Open the CSV up front and append+flush each point, so a long run that is
    # interrupted still leaves all completed points on disk.
    csv_file = None
    try:
        parent = os.path.dirname(CSV_PATH)
        if parent:
            os.makedirs(parent, exist_ok=True)
        csv_file = open(CSV_PATH, "w", encoding="utf-8")
        csv_file.write("carrier_hz,duty_pct,mark_us,cycles,frames,"
                       "mark_sd_us,mark_max_sd_us,mark_max_ptp_us,overall_mean_sd_us\n")
        csv_file.flush()
    except OSError as exc:
        print(f"CARRIER_PROBE_CSV_ERROR {exc}")

    rows = []
    for hz in CARRIERS:
        period_us = 1_000_000.0 / hz
        for duty in DUTIES:
            for mark in MARKS:
                aligned = collect_point(tx, rx, mark, hz, duty, FRAMES)
                if len(aligned) < 3:
                    print(
                        f"CARRIER_PROBE hz={hz} duty={duty:g} mark={mark} "
                        f"frames={len(aligned)} INSUFFICIENT"
                    )
                    continue
                sd, ptp = per_edge_stats(aligned)
                # The width-W marks are the even-index edges whose mean is near the
                # commanded mark width (excludes the 9000 header and all spaces).
                mean = [statistics.mean(c[i] for c in aligned) for i in range(len(sd))]
                mark_idx = [
                    i for i in range(0, len(sd), 2) if abs(mean[i] - mark) < 150
                ]
                if mark_idx:
                    mark_sd = statistics.mean(sd[i] for i in mark_idx)
                    mark_max_sd = max(sd[i] for i in mark_idx)
                    mark_max_ptp = max(ptp[i] for i in mark_idx)
                else:
                    mark_sd = mark_max_sd = mark_max_ptp = 0.0
                cycles = mark / period_us
                row = (hz, duty, mark, round(cycles, 3), len(aligned),
                       round(mark_sd, 2), round(mark_max_sd, 2), mark_max_ptp,
                       round(statistics.mean(sd), 2))
                rows.append(row)
                if csv_file:
                    csv_file.write(",".join(str(x) for x in row) + "\n")
                    csv_file.flush()
                print(
                    f"CARRIER_PROBE hz={hz} duty={duty:g} mark={mark} "
                    f"cycles={cycles:.3f} frames={len(aligned)} "
                    f"mark_sd={mark_sd:.2f} mark_max_sd={mark_max_sd:.2f} "
                    f"mark_max_ptp={mark_max_ptp} overall_mean_sd={statistics.mean(sd):.2f}"
                )

    if csv_file:
        csv_file.close()
        print(f"CARRIER_PROBE_CSV {CSV_PATH} rows={len(rows)}")

    record_property("points", len(rows))
    record_property("carriers", CARRIERS)
    record_property("duties", DUTIES)
    record_property("marks", MARKS)

    assert rows, "no measurement points collected"
