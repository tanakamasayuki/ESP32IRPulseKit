import re
from dataclasses import dataclass

import pytest
from pexpect import EOF, TIMEOUT


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
    Case("JVC24", 24, 0x00C0DE),
    Case("JVC32", 32, 0x1234ABCD),
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


def read_expected_decode(rx, case: Case):
    deadline_count = 0
    while deadline_count < 5:
        try:
            match = rx.expect(RX_DECODE, timeout=10)
        except (EOF, TIMEOUT):
            pytest.fail(
                f"RX did not decode {case.protocol} bits=0x{case.bits:x}.",
                pytrace=False,
            )

        protocol = match.group("protocol").decode()
        bit_length = int(match.group("length"))
        bits = int(match.group("bits"), 16)
        frame_type = match.group("type").decode()
        if protocol == case.protocol and bit_length == case.bit_length and bits == case.bits and frame_type == "NORMAL":
            return {
                "score": int(match.group("score")),
                "raw_len": int(match.group("raw_len")),
            }
        deadline_count += 1

    pytest.fail(
        f"RX decoded activity, but not expected {case.protocol} len={case.bit_length} bits=0x{case.bits:x}.",
        pytrace=False,
    )


@pytest.mark.parametrize("case", CASES, ids=lambda c: c.protocol)
def test_protocol_matrix_tx_rx(dut, peers, case, record_property):
    tx, rx = wait_boards_ready(dut, peers)
    assert_serial_control(tx, rx)

    tx.write(f"SEND {case.protocol} {case.bits:x}\n")
    tx.expect_exact(f"TX_OK {case.protocol} {case.bits:x}", timeout=5)

    observed = read_expected_decode(rx, case)
    record_property("protocol", case.protocol)
    record_property("bits", f"0x{case.bits:x}")
    record_property("score", observed["score"])
    record_property("raw_len", observed["raw_len"])
    print(
        f"PROTOCOL_MATRIX_OBSERVED protocol={case.protocol} "
        f"bits=0x{case.bits:x} score={observed['score']} raw_len={observed['raw_len']}"
    )
    # Keep this assertion loose: the matrix is for compatibility investigation,
    # and physical alignment affects score. A negative score would be suspicious
    # for self-generated ideal frames.
    assert observed["score"] >= 0
    assert observed["raw_len"] > 0
