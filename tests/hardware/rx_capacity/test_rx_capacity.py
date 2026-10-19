import re
from dataclasses import dataclass

import pytest
from pexpect import EOF, TIMEOUT

# RX capture capacity (setMaxRxSymbols) on real hardware. The peer sends a synthetic
# frame of N marks (2N-1 ticks, captured as N RMT symbols); the RX reports the
# captured length and the RAW_TRUNCATED flag.
#
# Requirements:
#   - a frame of at most `cap` symbols arrives intact and is not flagged truncated
#   - a longer frame is cut at `cap` symbols (2*cap ticks) and flagged truncated
#
# The default capacity (CAP 0) is 256 symbols.

RX_RAW = re.compile(rb"RX_RAW len=(\d+) truncated=([01])\r?\n")

TRIALS = 3


@dataclass(frozen=True)
class Case:
    cap: int
    pulses: int
    expect_len: int
    expect_truncated: bool

    @property
    def id(self):
        return f"cap{self.cap}-pulses{self.pulses}"


def intact(cap, pulses):
    return Case(cap, pulses, 2 * pulses - 1, False)


def truncated(cap, pulses):
    return Case(cap, pulses, 2 * cap, True)


CASES = [
    # Default capacity holds a long A/C-sized frame.
    intact(0, 200),
    # Explicit capacity: boundary at exactly cap symbols.
    intact(64, 64),
    truncated(64, 65),
    truncated(64, 200),
    # Larger than one RMT memory block on every SoC.
    intact(300, 300),
    truncated(300, 400),
]


def wait_boards_ready(dut, peers):
    tx = peers["tx"]
    rx = dut
    rx.write("READY\n")
    tx.write("READY\n")
    rx.expect(re.compile(rb"RX_READY gpio=\d+ inverted=[01]"), timeout=20)
    tx.expect(re.compile(rb"TX_READY gpio=\d+ inverted=[01]"), timeout=20)
    return tx, rx


def assert_serial_control(tx, rx):
    tx.write("PING\n")
    tx.expect_exact("PONG", timeout=5)
    rx.write("PING\n")
    rx.expect_exact("PONG", timeout=5)


def set_capacity(rx, cap):
    rx.write(f"CAP {cap}\n")
    rx.expect_exact(f"RX_CAP {cap}", timeout=5)


def send_and_capture(tx, rx, pulses):
    tx.write(f"SEND PULSES {pulses}\n")
    tx.expect_exact(f"TX_OK PULSES {pulses}", timeout=10)
    try:
        m = rx.expect(RX_RAW, timeout=5)
    except (EOF, TIMEOUT):
        return None
    return int(m.group(1)), m.group(2) == b"1"


@pytest.mark.parametrize("case", CASES, ids=[c.id for c in CASES])
def test_rx_capacity(dut, peers, case):
    tx, rx = wait_boards_ready(dut, peers)
    assert_serial_control(tx, rx)
    set_capacity(rx, case.cap)

    expected = (case.expect_len, case.expect_truncated)
    observed = []
    for _ in range(TRIALS):
        got = send_and_capture(tx, rx, case.pulses)
        observed.append(got)
        if got == expected:
            return

    pytest.fail(
        f"cap={case.cap} pulses={case.pulses}: expected (len, truncated)={expected}, "
        f"observed {observed}",
        pytrace=False,
    )
