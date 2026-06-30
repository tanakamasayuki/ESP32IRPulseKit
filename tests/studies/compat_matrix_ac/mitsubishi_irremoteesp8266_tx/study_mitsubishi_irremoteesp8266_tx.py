import re
from dataclasses import dataclass

import pytest
from pexpect import EOF, TIMEOUT

# AC field-map calibration (Mitsubishi): an external transmitter (IRremoteESP8266's
# IRMitsubishiAC) sends a KNOWN Mitsubishi A/C state; our RX (ESP32IRPulseKit)
# captures it RAW and decodes it with esp32irpk::ac::Mitsubishi.
#
# Two things are checked per case:
#   * HARD (pass/fail): our recovered 18 bytes equal the peer's canonical 18
#     bytes, with a valid checksum. This proves our RAW capture and framing are
#     correct, independent of field interpretation.
#   * SOFT (recorded + printed, never fails): our decoded fields vs the known sent
#     state. The bytes are authoritative ground truth, so a field mismatch means
#     the field map in src/ac/Mitsubishi.h needs adjusting -- it is reported, not
#     asserted, because calibrating that map is this study's whole purpose.
PEER_IMPL = "IRremoteESP8266"
DUT_IMPL = "ESP32IRPulseKit"

TRIALS = 5
PASS_MIN = 3

# Expected esp32irpk::ac::Mitsubishi enum underlying values for each symbolic
# state we ask the peer to send. Used only for the SOFT field comparison.
OUR_MODE = {"AUTO": 0, "COOL": 1, "HEAT": 2, "DRY": 3, "FAN": 4}
OUR_FAN = {"AUTO": 0, "QUIET": 1, "LOW": 2, "MED": 3, "HIGH": 4, "MAX": 5}

# The state is always 18 bytes = 36 hex chars. Match exactly 36 so pexpect does
# not lock in a partially-arrived serial buffer.
TX_OK_AC = re.compile(rb"TX_OK_AC vendor=MITSUBISHI bytes=(?P<bytes>[0-9A-Fa-f]{36})")

AC_DECODE = re.compile(
    rb"AC_DECODE vendor=MITSUBISHI checksum=(?P<checksum>ok|bad) "
    rb"power=(?P<power>\d+) mode=(?P<mode>\d+) temp=(?P<temp>\d+) "
    rb"fan=(?P<fan>\d+) bytes=(?P<bytes>[0-9A-Fa-f]{36})"
)


@dataclass(frozen=True)
class Case:
    mode: str
    fan: str
    temp: int
    power: int


# Spread of distinct states so the calibration table covers several field values.
CASES = [
    Case("COOL", "AUTO", 22, 1),
    Case("HEAT", "MAX", 24, 1),
    Case("DRY", "LOW", 26, 1),
    Case("FAN", "MED", 28, 1),
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
    try:
        match = rx.expect(AC_DECODE, timeout=3)
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


def capture_best_of_n(tx, rx, case: Case):
    """Send TRIALS times; count byte-exact + checksum-ok captures. Keep the last
    canonical (sent) bytes and the last decode for reporting."""
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
def test_mitsubishi_irremoteesp8266_tx(dut, peers, case, record_property):
    tx, rx = wait_boards_ready(dut, peers)
    assert_serial_control(tx, rx)

    sent, observed, n_ok = capture_best_of_n(tx, rx, case)
    record_property("byte_match_ratio", f"{n_ok}/{TRIALS}")

    # HARD check: our RAW framing must reproduce the canonical bytes.
    if n_ok < PASS_MIN:
        pytest.fail(
            f"{DUT_IMPL} RAW-decoded {case.mode}/{case.fan}/{case.temp}C/"
            f"power{case.power} to canonical bytes only {n_ok}/{TRIALS} times "
            f"(need >= {PASS_MIN}). sent={sent} observed={observed}",
            pytrace=False,
        )

    # SOFT check: field-map calibration. Reported, never fails.
    exp_mode = OUR_MODE[case.mode]
    exp_fan = OUR_FAN[case.fan]
    field_status = {
        "power": observed["power"] == case.power,
        "mode": observed["mode"] == exp_mode,
        "temp": observed["temp"] == case.temp,
        "fan": observed["fan"] == exp_fan,
    }
    for name, ok in field_status.items():
        record_property(f"field_{name}_match", ok)
    record_property("peer_impl", PEER_IMPL)
    record_property("dut_impl", DUT_IMPL)
    record_property("sent_state", f"{case.mode}/{case.fan}/{case.temp}C/p{case.power}")
    record_property("canonical_bytes", sent)
    record_property("our_mode", observed["mode"])
    record_property("our_temp", observed["temp"])
    record_property("our_fan", observed["fan"])
    record_property("our_power", observed["power"])

    def field(name, got, exp):
        return f"{name}={'ok' if got == exp else f'BAD(exp={exp},got={got})'}"

    print(
        f"COMPAT_MATRIX_AC_CAL peer={PEER_IMPL} dut={DUT_IMPL} "
        f"sent={case.mode}/{case.fan}/{case.temp}C/p{case.power} "
        f"byte_match={n_ok}/{TRIALS} canonical={sent} "
        f"{field('power', observed['power'], case.power)} "
        f"{field('mode', observed['mode'], exp_mode)} "
        f"{field('temp', observed['temp'], case.temp)} "
        f"{field('fan', observed['fan'], exp_fan)}"
    )
