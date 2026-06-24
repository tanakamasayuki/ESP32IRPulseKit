import re
from dataclasses import dataclass

import pytest
from pexpect import EOF, TIMEOUT

# AC self-test baseline: the SAME library (IRremoteESP8266) transmits via
# IRPanasonicAc and receives via IRrecv. The peer echoes the exact 27-byte state
# it sent (TX_OK_AC bytes=...); the primary decodes the captured burst and prints
# the 27-byte state it recovered (AC_DECODE bytes=...). If the bytes match with a
# valid checksum, the physical rig round-trips a Panasonic A/C frame -- which
# isolates placement/wiring problems from the cross-implementation field-map work
# in irremoteesp8266_tx / _rx. Field semantics are not under test here (encoder
# and decoder are the same library), only that the frame survives the air gap.
IMPL = "IRremoteESP8266"

TRIALS = 5
PASS_MIN = 3

TX_OK_AC = re.compile(rb"TX_OK_AC vendor=PANASONIC bytes=(?P<bytes>[0-9A-Fa-f]+)")

AC_DECODE = re.compile(
    rb"AC_DECODE vendor=PANASONIC checksum=(?P<checksum>ok|bad) "
    rb"power=(?P<power>\d+) mode=(?P<mode>\d+) temp=(?P<temp>\d+) "
    rb"fan=(?P<fan>\d+) bytes=(?P<bytes>[0-9A-Fa-f]+)"
)


@dataclass(frozen=True)
class Case:
    mode: str
    fan: str
    temp: int
    power: int


# A spread of distinct states so the baseline also shows that different fields
# produce different frames (not just one fixed pattern round-tripping).
CASES = [
    Case("COOL", "AUTO", 26, 1),
    Case("HEAT", "HIGH", 22, 1),
    Case("DRY", "LOW", 24, 1),
    Case("COOL", "MED", 18, 1),
    Case("AUTO", "AUTO", 25, 0),
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


def send_once(tx, case: Case):
    """Transmit one frame; return the 27-byte state the peer reports sending, or
    None if the peer never acknowledged."""
    tx.write(
        f"SEND_AC mode={case.mode} fan={case.fan} "
        f"temp={case.temp} power={case.power}\n"
    )
    try:
        match = tx.expect(TX_OK_AC, timeout=5)
    except (EOF, TIMEOUT):
        return None
    return match.group("bytes").decode().lower()


def decode_once(rx):
    """Read one AC decode. Return a dict, or None if nothing decodable arrived."""
    try:
        match = rx.expect(AC_DECODE, timeout=12)
    except (EOF, TIMEOUT):
        return None
    return {
        "checksum": match.group("checksum").decode(),
        "power": int(match.group("power")),
        "mode": int(match.group("mode")),
        "temp": int(match.group("temp")),
        "fan": int(match.group("fan")),
        "bytes": match.group("bytes").decode().lower(),
    }


def roundtrip_best_of_n(tx, rx, case: Case):
    """Send the state TRIALS times; count how often the decoded bytes match the
    sent bytes with a valid checksum. Majority pass tolerates a single disturbed
    frame while a genuinely unusable link still fails."""
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
        if obs["checksum"] == "ok" and obs["bytes"] == sent:
            n_ok += 1
    return sent_last, obs_last, n_ok


@pytest.mark.parametrize(
    "case", CASES, ids=lambda c: f"{c.mode}_{c.fan}_{c.temp}_p{c.power}"
)
def test_irremoteesp8266_self(dut, peers, case, record_property):
    tx, rx = wait_boards_ready(dut, peers)
    assert_serial_control(tx, rx)

    sent, observed, n_ok = roundtrip_best_of_n(tx, rx, case)
    record_property("roundtrip_ratio", f"{n_ok}/{TRIALS}")
    if n_ok < PASS_MIN:
        pytest.fail(
            f"{IMPL} AC self-test round-tripped {case.mode}/{case.fan}/"
            f"{case.temp}C/power{case.power} only {n_ok}/{TRIALS} times "
            f"(need >= {PASS_MIN}); placement/environment too marginal even for "
            f"the library talking to itself. sent={sent} observed={observed}",
            pytrace=False,
        )

    record_property("impl", IMPL)
    record_property("sent_state", f"{case.mode}/{case.fan}/{case.temp}C/p{case.power}")
    record_property("sent_bytes", sent)
    record_property("rx_bytes", observed["bytes"])
    record_property("rx_power", observed["power"])
    record_property("rx_mode", observed["mode"])
    record_property("rx_temp", observed["temp"])
    record_property("rx_fan", observed["fan"])
    print(
        f"COMPAT_MATRIX_AC_SELF impl={IMPL} "
        f"sent={case.mode}/{case.fan}/{case.temp}C/p{case.power} "
        f"sent_bytes={sent} rx_bytes={observed['bytes']} "
        f"rx_power={observed['power']} rx_mode={observed['mode']} "
        f"rx_temp={observed['temp']} rx_fan={observed['fan']} "
        f"ratio={n_ok}/{TRIALS}"
    )
