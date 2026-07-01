import re
from dataclasses import dataclass

import pytest
from pexpect import EOF, TIMEOUT

# A/C protocol matrix: the ac:: analog of protocol_matrix. Both TX and RX are
# ESP32IRPulseKit, so this is a self round-trip gate for the air-conditioner layer
# on real hardware -- no external library. The peer sends each vendor's default
# known-good state via ac::send; the RX primary identifies it with ac::decodeAny
# and re-decodes the matched vendor's Frame. A case passes when the recovered
# vendor + state bytes equal what the peer reported sending, with a valid checksum.
#
# Cross-implementation interop (IRremoteESP8266, HeatpumpIR) is a separate concern
# covered by studies/compat_matrix_ac.

# NOTE: the trailing \r?\n is required. Vendors have different byte counts, so the
# hex group is variable-length; without the newline anchor pexpect matches a partial
# prefix as soon as the first serial chunk arrives (e.g. "aa5a") instead of waiting
# for the whole line. Anchoring on the line end forces the full state to be captured.
TX_OK_AC = re.compile(
    rb"TX_OK_AC vendor=(?P<vendor>[A-Z]+) bytes=(?P<bytes>[0-9A-Fa-f]+)\r?\n"
)
AC_DECODE = re.compile(
    rb"AC_DECODE vendor=(?P<vendor>[A-Z]+) checksum=(?P<checksum>ok|bad) "
    rb"bytes=(?P<bytes>[0-9A-Fa-f]+)\r?\n"
)

TRIALS = 5
PASS_MIN = 3

# One representative state per vendor (the Frame's built-in default template). The
# peer builds it from the vendor name alone, so the test only needs the names.
VENDORS = [
    "PANASONIC",
    "GREE",
    "MITSUBISHI",
    "FUJITSU",
    "DAIKIN",
    "TOSHIBA",
    "SAMSUNG",
    "SHARP",
    "KELVINATOR",
]


@dataclass(frozen=True)
class Case:
    vendor: str


CASES = [Case(v) for v in VENDORS]


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


def send_once(tx, case: Case):
    tx.write(f"SEND_AC vendor={case.vendor}\n")
    try:
        m = tx.expect(TX_OK_AC, timeout=5)
    except (EOF, TIMEOUT):
        return None
    if m.group("vendor").decode() != case.vendor:
        return None
    return m.group("bytes").decode().lower()


def decode_once(rx):
    try:
        m = rx.expect(AC_DECODE, timeout=12)
    except (EOF, TIMEOUT):
        return None
    return {
        "vendor": m.group("vendor").decode(),
        "checksum": m.group("checksum").decode(),
        "bytes": m.group("bytes").decode().lower(),
    }


def roundtrip_best_of_n(tx, rx, case: Case):
    """Send TRIALS times; count trials where the RX recovered the same vendor and
    bytes with a valid checksum. Majority-judged so one disturbed frame does not
    fail an otherwise-working link."""
    n_ok = 0
    sent_last = None
    obs_last = None
    for _ in range(TRIALS):
        sent = send_once(tx, case)
        if sent is None:
            continue
        sent_last = sent
        obs = decode_once(rx)
        if obs is None:
            continue
        obs_last = obs
        if obs["vendor"] == case.vendor and obs["checksum"] == "ok" and obs["bytes"] == sent:
            n_ok += 1
    return sent_last, obs_last, n_ok


@pytest.mark.parametrize("case", CASES, ids=lambda c: c.vendor)
def test_protocol_matrix_ac_tx_rx(dut, peers, case, record_property):
    tx, rx = wait_boards_ready(dut, peers)
    assert_serial_control(tx, rx)

    sent, observed, n_ok = roundtrip_best_of_n(tx, rx, case)
    record_property("vendor", case.vendor)
    record_property("roundtrip_ratio", f"{n_ok}/{TRIALS}")
    record_property("sent_bytes", sent)
    if observed is not None:
        record_property("rx_bytes", observed["bytes"])

    if n_ok < PASS_MIN:
        pytest.fail(
            f"{case.vendor} self round-trip decoded to the sent bytes only "
            f"{n_ok}/{TRIALS} times (need >= {PASS_MIN}). sent={sent} observed={observed}",
            pytrace=False,
        )

    print(
        f"PROTOCOL_MATRIX_AC vendor={case.vendor} ratio={n_ok}/{TRIALS} bytes={sent}"
    )
