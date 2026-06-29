import re
from dataclasses import dataclass

import pytest
from pexpect import EOF, TIMEOUT

# AC field-map calibration (Fujitsu): an external transmitter (IRremoteESP8266's
# IRFujitsuAC, model ARRAH2E) sends a KNOWN Fujitsu A/C state; our RX
# (ESP32IRPulseKit) captures it RAW and decodes it with esp32irpk::ac::Fujitsu.
#
# Two things are checked per case:
#   * HARD (pass/fail): our recovered bytes equal the peer's canonical bytes
#     (16 for a long frame, 7 for a power-off short frame), with a valid checksum.
#     This proves our RAW capture and framing are correct, independent of field
#     interpretation.
#   * SOFT (recorded + printed, never fails): our decoded fields vs the known sent
#     state. The bytes are authoritative ground truth, so a field mismatch means
#     the field map in src/ac/Fujitsu.h needs adjusting -- it is reported, not
#     asserted, because calibrating that map is this study's whole purpose.
#
# Our esp32irpk::ac::Fujitsu enum underlying values equal the IRremoteESP8266 wire
# codes (Mode auto=0..heat=4, Fan auto=0/high=1/med=2/low=3/quiet=4, Swing
# off=0..both=3), so the SOFT comparison is a direct numeric match.
PEER_IMPL = "IRremoteESP8266"
DUT_IMPL = "ESP32IRPulseKit"

TRIALS = 5
PASS_MIN = 3

OUR_MODE = {"AUTO": 0, "COOL": 1, "DRY": 2, "FAN": 3, "HEAT": 4}
OUR_FAN = {"AUTO": 0, "HIGH": 1, "MED": 2, "LOW": 3, "QUIET": 4}
OUR_SWING = {"OFF": 0, "VERT": 1, "HORIZ": 2, "BOTH": 3}

# A long frame is 16 bytes = 32 hex chars; a power-off short frame is 7 bytes = 14
# hex chars. Anchor on the trailing newline so a partially-arrived buffer is not
# matched mid-line.
TX_OK_AC = re.compile(rb"TX_OK_AC vendor=FUJITSU bytes=(?P<bytes>[0-9A-Fa-f]{14,32})\r?\n")

AC_DECODE = re.compile(
    rb"AC_DECODE vendor=FUJITSU checksum=(?P<checksum>ok|bad) "
    rb"power=(?P<power>\d+) mode=(?P<mode>\d+) temp=(?P<temp>\d+) "
    rb"fan=(?P<fan>\d+) swing=(?P<swing>\d+) "
    rb"bytes=(?P<bytes>[0-9A-Fa-f]{14,32})\r?\n"
)


@dataclass(frozen=True)
class Case:
    mode: str
    fan: str
    temp: int
    swing: str
    power: int


# Spread of distinct states so the calibration table covers several field values,
# plus a power-off case that exercises the 7-byte short frame.
CASES = [
    Case("COOL", "AUTO", 22, "OFF", 1),
    Case("HEAT", "HIGH", 24, "VERT", 1),
    Case("DRY", "LOW", 26, "OFF", 1),
    Case("FAN", "MED", 28, "BOTH", 1),
    Case("COOL", "QUIET", 20, "HORIZ", 1),
    Case("AUTO", "AUTO", 25, "OFF", 0),
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
        f"SEND_AC mode={case.mode} fan={case.fan} temp={case.temp} "
        f"swing={case.swing} power={case.power}\n"
    )
    try:
        match = tx.expect(TX_OK_AC, timeout=5)
    except (EOF, TIMEOUT):
        return None
    return match.group("bytes").decode().lower()


def decode_once(rx):
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
        "swing": int(match.group("swing")),
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
    "case", CASES, ids=lambda c: f"{c.mode}_{c.fan}_{c.temp}_{c.swing}_p{c.power}"
)
def test_fujitsu_irremoteesp8266_tx(dut, peers, case, record_property):
    tx, rx = wait_boards_ready(dut, peers)
    assert_serial_control(tx, rx)

    sent, observed, n_ok = capture_best_of_n(tx, rx, case)
    record_property("byte_match_ratio", f"{n_ok}/{TRIALS}")

    # HARD check: our RAW framing must reproduce the canonical bytes.
    if n_ok < PASS_MIN:
        pytest.fail(
            f"{DUT_IMPL} RAW-decoded {case.mode}/{case.fan}/{case.temp}C/{case.swing}/"
            f"power{case.power} to canonical bytes only {n_ok}/{TRIALS} times "
            f"(need >= {PASS_MIN}). sent={sent} observed={observed}",
            pytrace=False,
        )

    # SOFT check: field-map calibration. Reported, never fails. On power-off the
    # short frame carries no mode/temp/fan/swing (vendor don't-cares), so only the
    # power field is meaningful.
    exp_mode = OUR_MODE[case.mode]
    exp_fan = OUR_FAN[case.fan]
    exp_swing = OUR_SWING[case.swing]
    if case.power:
        field_status = {
            "power": observed["power"] == case.power,
            "mode": observed["mode"] == exp_mode,
            "temp": observed["temp"] == case.temp,
            "fan": observed["fan"] == exp_fan,
            "swing": observed["swing"] == exp_swing,
        }
    else:
        field_status = {"power": observed["power"] == case.power}
    for name, ok in field_status.items():
        record_property(f"field_{name}_match", ok)
    record_property("peer_impl", PEER_IMPL)
    record_property("dut_impl", DUT_IMPL)
    record_property(
        "sent_state", f"{case.mode}/{case.fan}/{case.temp}C/{case.swing}/p{case.power}"
    )
    record_property("canonical_bytes", sent)
    record_property("our_mode", observed["mode"])
    record_property("our_temp", observed["temp"])
    record_property("our_fan", observed["fan"])
    record_property("our_swing", observed["swing"])
    record_property("our_power", observed["power"])

    def field(name, got, exp):
        return f"{name}={'ok' if got == exp else f'BAD(exp={exp},got={got})'}"

    print(
        f"COMPAT_MATRIX_AC_CAL peer={PEER_IMPL} dut={DUT_IMPL} "
        f"sent={case.mode}/{case.fan}/{case.temp}C/{case.swing}/p{case.power} "
        f"byte_match={n_ok}/{TRIALS} canonical={sent} "
        f"{field('power', observed['power'], case.power)} "
        f"{field('mode', observed['mode'], exp_mode)} "
        f"{field('temp', observed['temp'], case.temp)} "
        f"{field('fan', observed['fan'], exp_fan)} "
        f"{field('swing', observed['swing'], exp_swing)}"
    )
