import os
import re
from dataclasses import dataclass

import pytest
from pexpect import EOF, TIMEOUT


# PulseKit TX carrier mode for this run: "hw" (default, free-running hardware
# carrier) or "pa" (phase-aligned). Set PULSEKIT_CARRIER=pa to run the whole
# self-matrix under the phase-aligned path (see studies/phase_aligned_carrier).
CARRIER_MODE = os.environ.get("PULSEKIT_CARRIER", "hw")


RX_DECODE = re.compile(
    rb"RX_DECODE protocol=(?P<protocol>[A-Z0-9_]+) "
    rb"score=(?P<score>-?\d+) "
    rb"len=(?P<length>\d+) "
    rb"bits=0x(?P<bits>[0-9A-Fa-f]+) "
    rb"type=(?P<type>NORMAL|REPEAT) "
    rb"raw_len=(?P<raw_len>\d+)"
)


@dataclass(frozen=True)
class Case:
    protocol: str
    bit_length: int
    bits: int


CASES = [
    Case("NEC", 32, 0xCB3400FF),
    Case("SONY12", 12, 0x0A90),
    Case("SONY15", 15, 0x3456),
    Case("SONY20", 20, 0xABCDE),
    Case("SAMSUNG32", 32, 0x40BFE0E0),
    Case("SAMSUNG36", 36, 0xABCDEABCD),
    Case("JVC", 16, 0xC0DE),
    Case("AEHA", 48, 0x123456749ABC),
    Case("RC5", 14, 0x300F),
    Case("RC6_M0_16", 21, 0x111234),
    Case("RC6_M6_32", 36, 0xE89ABCDEF),
]


def wait_boards_ready(dut, peers):
    tx = peers["tx"]
    rx = dut
    rx.write("READY\n")
    tx.write("READY\n")
    rx.expect(re.compile(rb"RX_READY impl=ESP32IRPulseKit gpio=\d+ inverted=[01]"), timeout=20)
    tx.expect(re.compile(rb"TX_READY impl=ESP32IRPulseKit gpio=\d+ inverted=[01]"), timeout=20)
    return tx, rx


def assert_serial_control(tx, rx):
    tx.write("PING\n")
    tx.expect_exact("PONG", timeout=5)
    rx.write("PING\n")
    rx.expect_exact("PONG", timeout=5)


def set_pulsekit_carrier(tx, mode):
    """Select the PulseKit TX carrier mode on the peer (hw|pa). A pa switch
    re-creates the TX channel, so allow extra time for the reply."""
    tx.write(f"CARRIER {mode}\n")
    tx.expect(re.compile(rb"CARRIER_OK mode=(hw|pa)"), timeout=10)


TRIALS = 5
PASS_MIN = 3


def try_decode_once(rx, case: Case):
    """Read one RX_DECODE; return {score, raw_len} when it matches the expected
    protocol/len/bits/NORMAL, else None (so the caller can judge by majority)."""
    try:
        match = rx.expect(RX_DECODE, timeout=10)
    except (EOF, TIMEOUT):
        return None
    protocol = match.group("protocol").decode()
    bit_length = int(match.group("length"))
    bits = int(match.group("bits"), 16)
    frame_type = match.group("type").decode()
    if (protocol == case.protocol and bit_length == case.bit_length
            and bits == case.bits and frame_type == "NORMAL"):
        return {"score": int(match.group("score")), "raw_len": int(match.group("raw_len"))}
    return None


def decode_best_of_n(tx, rx, case: Case):
    """Send the frame TRIALS times; return (last_observed, success_count).
    Judged by majority so a single disturbed/dropped frame does not fail the
    case, while a genuinely incompatible link (never decodes) still fails."""
    n_ok = 0
    last = None
    for _ in range(TRIALS):
        tx.write(f"SEND {case.protocol} {case.bits:x}\n")
        try:
            tx.expect_exact(f"TX_OK {case.protocol} {case.bits:x}", timeout=5)
        except (EOF, TIMEOUT):
            continue
        obs = try_decode_once(rx, case)
        if obs:
            n_ok += 1
            last = obs
    return last, n_ok

@pytest.mark.parametrize("case", CASES, ids=lambda c: c.protocol)
def test_protocol_matrix_tx_rx(dut, peers, case, record_property):
    tx, rx = wait_boards_ready(dut, peers)
    assert_serial_control(tx, rx)
    set_pulsekit_carrier(tx, CARRIER_MODE)

    observed, n_ok = decode_best_of_n(tx, rx, case)
    record_property("decode_ratio", f"{n_ok}/{TRIALS}")
    if n_ok < PASS_MIN:
        pytest.fail(
            f"{case.protocol} decoded only {n_ok}/{TRIALS} times "
            f"(need >= {PASS_MIN}); link too marginal or incompatible.",
            pytrace=False,
        )
    record_property("protocol", case.protocol)
    record_property("pulsekit_carrier", CARRIER_MODE)
    record_property("bits", f"0x{case.bits:x}")
    record_property("score", observed["score"])
    record_property("raw_len", observed["raw_len"])
    print(
        f"PROTOCOL_MATRIX_OBSERVED carrier={CARRIER_MODE} protocol={case.protocol} "
        f"bits=0x{case.bits:x} score={observed['score']} raw_len={observed['raw_len']}"
    )
    # Keep this assertion loose: the matrix is for compatibility investigation,
    # and physical alignment affects score. A negative score would be suspicious
    # for self-generated ideal frames.
    assert observed["score"] >= 0
    assert observed["raw_len"] > 0
