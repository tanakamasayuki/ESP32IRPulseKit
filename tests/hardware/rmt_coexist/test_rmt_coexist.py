import re

import pytest

# Single-board RMT coexistence: the library's RX and TX channels, and another RMT
# user (a WS2812-style 10MHz channel), created in every order on one board must
# all begin and send. SoCs other than ESP32 / ESP32-S2 share one RMT group clock
# picked by the first channel, which used to make a 100kHz RX fail after a 1MHz TX.

CASE = re.compile(rb"CASE (\S+) (OK|FAIL)([^\r\n]*)\r?\n")
CASES = 6


def test_rmt_coexist(dut):
    dut.write("READY\n")
    dut.expect_exact("COEXIST_READY", timeout=20)
    dut.write("RUN\n")

    results = []
    for _ in range(CASES):
        m = dut.expect(CASE, timeout=10)
        results.append((m.group(1).decode(), m.group(2) == b"OK", m.group(3).decode().strip()))
    dut.expect_exact("DONE", timeout=5)

    failed = [f"{name}: {detail}" for name, ok, detail in results if not ok]
    if failed:
        pytest.fail("RMT channels failed to coexist:\n  " + "\n  ".join(failed), pytrace=False)
