import re
from dataclasses import dataclass

import pytest
from pexpect import EOF, TIMEOUT

# Self-test baseline: the SAME library transmits and receives (peer TX and the
# primary RX are both Arduino-IRremote). If a case fails here, the placement/environment
# is too harsh for the library to even decode its own transmitter -- which
# isolates environment problems from cross-implementation (ESP32IRPulseKit)
# timing/duty differences seen in the other compat_matrix variants.
IMPL = "Arduino-IRremote"

TRIALS = 5
PASS_MIN = 3


def bit_reverse(value: int, bit_length: int) -> int:
    result = 0
    for i in range(bit_length):
        result = (result << 1) | ((value >> i) & 1)
    return result


def classify_bit_order(sent: int, observed: int, bit_length: int) -> str:
    if observed == sent:
        return "same"
    if observed == bit_reverse(sent, bit_length):
        return "reversed"
    return "other"


RX_RESULT = re.compile(
    rb"RX_DECODE protocol=(?P<protocol>[A-Z0-9_]+) "
    rb"score=(?P<score>-?\d+) "
    rb"len=(?P<length>\d+) "
    rb"bits=0x(?P<bits>[0-9A-Fa-f]+) "
    rb"type=(?P<type>NORMAL|REPEAT) "
    rb"raw_len=(?P<raw_len>\d+)"
    rb"|RX_RAW len=(?P<raw_only_len>\d+)"
)


@dataclass(frozen=True)
class Case:
    protocol: str
    bits: int


CASES = [
    Case("NEC", 0xCB3400FF),
    Case("SONY12", 0x0A90),
    Case("SONY15", 0x3456),
    Case("SONY20", 0xABCDE),
    Case("SAMSUNG32", 0x40BFE0E0),
    Case("SAMSUNG36", 0xABCDEABCD),
    Case("JVC", 0xC0DE),
]


def wait_boards_ready(dut, peers):
    tx = peers["tx"]
    rx = dut
    rx.write("READY\n")
    tx.write("READY\n")
    rx.expect(re.compile(rb"RX_READY impl=\S+ gpio=\d+ inverted=[01]"), timeout=20)
    tx.expect(re.compile(rb"TX_READY impl=\S+ gpio=\d+ inverted=[01]"), timeout=20)
    return tx, rx


def assert_serial_control(tx, rx):
    tx.write("PING\n")
    tx.expect_exact("PONG", timeout=5)
    rx.write("PING\n")
    rx.expect_exact("PONG", timeout=5)


def try_decode_once(rx, case: Case):
    """Read one RX result. Return the observed decode dict, or None when the RX
    saw no signal, dumped RX_RAW (undecodable), or produced a spurious decode."""
    try:
        match = rx.expect(RX_RESULT, timeout=12)
    except (EOF, TIMEOUT):
        return None
    if match.group("protocol") is None:
        return None
    protocol = match.group("protocol").decode()
    bit_length = int(match.group("length"))
    if bit_length == 0 or protocol.startswith("OTHER_"):
        return None
    return {
        "protocol": protocol,
        "bit_length": bit_length,
        "bits": int(match.group("bits"), 16),
        "type": match.group("type").decode(),
        "score": int(match.group("score")),
        "raw_len": int(match.group("raw_len")),
    }


def decode_best_of_n(tx, rx, case: Case):
    """Send the frame TRIALS times; return (last_observed, success_count).
    Judged by majority so a single disturbed/dropped frame does not fail the
    case, while a genuinely unusable link (never decodes) still fails."""
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
def test_arduino_irremote_self(dut, peers, case, record_property):
    tx, rx = wait_boards_ready(dut, peers)
    assert_serial_control(tx, rx)

    observed, n_ok = decode_best_of_n(tx, rx, case)
    record_property("decode_ratio", f"{n_ok}/{TRIALS}")
    if n_ok < PASS_MIN:
        pytest.fail(
            f"{IMPL} self-test decoded {case.protocol} only {n_ok}/{TRIALS} times "
            f"(need >= {PASS_MIN}); placement/environment too marginal even for the "
            f"library talking to itself.",
            pytrace=False,
        )

    bits_match = observed["bits"] == case.bits
    bit_order = classify_bit_order(case.bits, observed["bits"], observed["bit_length"])
    record_property("impl", IMPL)
    record_property("sent_protocol", case.protocol)
    record_property("sent_bits", f"0x{case.bits:x}")
    record_property("rx_protocol", observed["protocol"])
    record_property("rx_bits", f"0x{observed['bits']:x}")
    record_property("bits_match", bits_match)
    record_property("bit_order", bit_order)
    record_property("raw_len", observed["raw_len"])
    print(
        f"COMPAT_MATRIX_SELF impl={IMPL} sent={case.protocol} "
        f"sent_bits=0x{case.bits:x} rx_protocol={observed['protocol']} "
        f"rx_bits=0x{observed['bits']:x} ratio={n_ok}/{TRIALS} "
        f"bits={'match' if bits_match else 'diff'} bit_order={bit_order}"
    )

    assert observed["raw_len"] > 0
