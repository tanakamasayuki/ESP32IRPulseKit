import os
import re

import pytest
from pexpect import EOF, TIMEOUT

# Overfit cross-check: the jvc_timing_sweep winner was tuned to IRremoteESP8266.
# Here the same tunable JVC peer (JVCRAW) transmits to an *Arduino-IRremote* RX,
# comparing the JVC standard timing against the tuned candidate, so we can tell
# whether the tuning that helps IRremoteESP8266 also decodes on a second
# independent receiver (i.e. is not overfit).
ONE_US = int(os.environ.get("JV_ONE", "1575"))
N = int(os.environ.get("JV_N", "15"))
BITS = os.environ.get("JV_BITS", "c0de")

# (mark_us, zero_space_us, label)
POINTS = [
    (530, 530, "std (JVC default 525->530)"),
    (540, 480, "tuned (sweep winner)"),
    (540, 470, "tuned-lo"),
    (520, 530, "low-mark (control)"),
]

RX_RESULT = re.compile(
    rb"RX_DECODE protocol=(?P<protocol>[A-Z0-9_]+) score=-?\d+ len=(?P<length>\d+) "
    rb"bits=0x(?P<bits>[0-9A-Fa-f]+) type=\S+ raw_len=(?P<raw_len>\d+)"
    rb"|RX_RAW len=(?P<raw_only_len>\d+)"
)


def wait_boards_ready(dut, peers):
    tx, rx = peers["tx"], dut
    rx.write("READY\n")
    tx.write("READY\n")
    rx.expect(re.compile(rb"RX_READY impl=\S+"), timeout=20)
    tx.expect(re.compile(rb"TX_READY impl=\S+"), timeout=20)
    return tx, rx


def read_one(rx, timeout=8):
    """Return decoded protocol name, or None (RX_RAW / no signal)."""
    try:
        m = rx.expect(RX_RESULT, timeout=timeout)
    except (EOF, TIMEOUT):
        return None
    if m.group("protocol") is None:
        return None
    return m.group("protocol").decode()


def test_jvc_verify_arduino(dut, peers, record_property):
    tx, rx = wait_boards_ready(dut, peers)
    tx.write("PING\n"); tx.expect_exact("PONG", timeout=5)
    rx.write("PING\n"); rx.expect_exact("PONG", timeout=5)

    print(f"\nJVC_VERIFY rx=Arduino-IRremote one={ONE_US} N={N} bits=0x{BITS}")
    results = {}
    for mark, zsp, label in POINTS:
        decoded = 0
        protos = set()
        for _ in range(N):
            tx.write(f"JVCRAW {mark} {zsp} {ONE_US} {BITS}\n")
            try:
                tx.expect_exact("JVCRAW_OK", timeout=5)
            except (EOF, TIMEOUT):
                continue
            proto = read_one(rx)
            if proto and proto.startswith("JVC"):
                decoded += 1
            if proto:
                protos.add(proto)
        ratio = decoded / N
        results[label] = ratio
        record_property(f"pass_{mark}_{zsp}", round(ratio, 3))
        print(f"  mark={mark} zsp={zsp:<4} {label:<28} JVC {decoded}/{N} "
              f"({ratio:.0%})  saw={sorted(protos) or '-'}")

    assert results, "no points measured (check 2-board IR wiring TX->RX)"
