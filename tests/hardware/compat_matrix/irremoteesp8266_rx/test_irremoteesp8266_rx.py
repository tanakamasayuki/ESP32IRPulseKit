import re
from dataclasses import dataclass

import pytest
from pexpect import EOF, TIMEOUT


def bit_reverse(value: int, bit_length: int) -> int:
    result = 0
    for i in range(bit_length):
        result = (result << 1) | ((value >> i) & 1)
    return result


def classify_bit_order(sent: int, observed: int, bit_length: int) -> str:
    """Compare the sent payload against the RX-observed payload.

    The physical bitstream is identical between implementations; only the
    integer representation can differ. "same" means matching endianness,
    "reversed" means a pure MSB/LSB-first convention difference (the common
    case across libraries), "other" means a genuine value mismatch worth a
    closer look.
    """
    if observed == sent:
        return "same"
    if observed == bit_reverse(sent, bit_length):
        return "reversed"
    return "other"


# Match either a successful decode or a received-but-undecodable RX_RAW dump.
# "protocol" group is None when the RX_RAW branch matched.
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
    # protocol/bits: payload the ESP32IRPulseKit TX peer transmits. The
    # IRremoteESP8266 RX may decode it with a different value due to
    # bit-order/field differences, which is exactly what this matrix observes.
    protocol: str
    bits: int


CASES = [
    Case("NEC", 0xCB3400FF),
    Case("SONY12", 0x0A90),
    Case("SAMSUNG32", 0x40BFE0E0),
    Case("JVC24", 0x00C0DE),
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


def read_first_decode(rx, case: Case):
    try:
        match = rx.expect(RX_RESULT, timeout=12)
    except (EOF, TIMEOUT):
        pytest.fail(
            f"IRremoteESP8266 RX saw no IR signal for {case.protocol} "
            f"bits=0x{case.bits:x} (no RX_DECODE or RX_RAW within timeout).",
            pytrace=False,
        )
    if match.group("protocol") is None:
        raw_len = int(match.group("raw_only_len"))
        pytest.fail(
            f"IRremoteESP8266 RX received a frame (raw_len={raw_len}) but could "
            f"not decode {case.protocol} bits=0x{case.bits:x} — likely a "
            f"timing/tolerance incompatibility.",
            pytrace=False,
        )
    protocol = match.group("protocol").decode()
    bit_length = int(match.group("length"))
    if bit_length == 0 or protocol.startswith("OTHER_"):
        pytest.fail(
            f"RX produced a spurious decode (protocol={protocol}, len={bit_length}) "
            f"for {case.protocol} bits=0x{case.bits:x} — no usable protocol/bits, "
            f"treated as undecodable.",
            pytrace=False,
        )
    return {
        "protocol": protocol,
        "bit_length": bit_length,
        "bits": int(match.group("bits"), 16),
        "type": match.group("type").decode(),
        "score": int(match.group("score")),
        "raw_len": int(match.group("raw_len")),
    }


@pytest.mark.parametrize("case", CASES, ids=lambda c: c.protocol)
def test_pulsekit_tx_to_irremoteesp8266_rx(dut, peers, case, record_property):
    tx, rx = wait_boards_ready(dut, peers)
    assert_serial_control(tx, rx)

    tx.write(f"SEND {case.protocol} {case.bits:x}\n")
    tx.expect_exact(f"TX_OK {case.protocol} {case.bits:x}", timeout=5)

    observed = read_first_decode(rx, case)
    bits_match = observed["bits"] == case.bits
    bit_order = classify_bit_order(case.bits, observed["bits"], observed["bit_length"])

    record_property("rx_impl", "IRremoteESP8266")
    record_property("sent_protocol", case.protocol)
    record_property("sent_bits", f"0x{case.bits:x}")
    record_property("rx_protocol", observed["protocol"])
    record_property("rx_bits", f"0x{observed['bits']:x}")
    record_property("rx_bit_length", observed["bit_length"])
    record_property("bits_match", bits_match)
    record_property("bit_order", bit_order)
    record_property("raw_len", observed["raw_len"])
    print(
        "COMPAT_MATRIX_OBSERVED rx=IRremoteESP8266 "
        f"sent={case.protocol} sent_bits=0x{case.bits:x} "
        f"rx_protocol={observed['protocol']} rx_bits=0x{observed['bits']:x} "
        f"rx_len={observed['bit_length']} "
        f"raw_len={observed['raw_len']} bits={'match' if bits_match else 'diff'} "
        f"bit_order={bit_order}"
    )

    # compat_matrix records cross-implementation differences as observations.
    # Bit order and field interpretation may differ from ESP32IRPulseKit, so do
    # not assert an exact protocol/bits match here. Only require that the RX
    # recognized a real frame.
    assert observed["raw_len"] > 0
