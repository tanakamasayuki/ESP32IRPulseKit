import re
from dataclasses import dataclass

import pytest
from pexpect import EOF, TIMEOUT

# AC field-map calibration (Sharp): IRremoteESP8266's IRSharpAc (standard 13-byte
# SHARP_AC, default A907 model) transmits a KNOWN state; our RX captures it RAW and
# decodes it with esp32irpk::ac::Sharp.
#
#   * HARD (pass/fail): our recovered 13 bytes equal the peer's canonical 13 bytes,
#     with a valid nibble checksum. Proves our RAW capture + framing.
#   * SOFT (recorded): our decoded fields vs the known state (calibration). Temp is
#     a don't-care in Auto/Dry (those modes carry no temperature).
#
# Our enum underlying values equal the wire codes (Mode auto=0/heat=1/cool=2/dry=3;
# Fan auto=2/med=3/min=4/high=5/max=7), so the comparison is a direct match.
PEER_IMPL = "IRremoteESP8266"
DUT_IMPL = "ESP32IRPulseKit"

TRIALS = 5
PASS_MIN = 3

OUR_MODE = {"AUTO": 0, "HEAT": 1, "COOL": 2, "DRY": 3}
OUR_FAN = {"AUTO": 2, "MED": 3, "MIN": 4, "HIGH": 5, "MAX": 7}

# 13 bytes = 26 hex chars.
TX_OK_AC = re.compile(rb"TX_OK_AC vendor=SHARP bytes=(?P<bytes>[0-9A-Fa-f]{26})\r?\n")
AC_DECODE = re.compile(
    rb"AC_DECODE vendor=SHARP checksum=(?P<checksum>ok|bad) "
    rb"power=(?P<power>\d+) mode=(?P<mode>\d+) temp=(?P<temp>\d+) "
    rb"fan=(?P<fan>\d+) bytes=(?P<bytes>[0-9A-Fa-f]{26})\r?\n"
)


@dataclass(frozen=True)
class Case:
    mode: str
    fan: str
    temp: int
    power: int


CASES = [
    Case("COOL", "AUTO", 24, 1),
    Case("HEAT", "MAX", 28, 1),
    Case("COOL", "MIN", 18, 1),
    Case("HEAT", "MED", 26, 1),
    Case("DRY", "HIGH", 22, 1),
    Case("AUTO", "AUTO", 25, 0),
]


def wait_boards_ready(dut, peers):
    tx = peers["tx"]
    rx = dut
    rx.write("READY\n")
    tx.write("READY\n")
    rx.expect(re.compile(rb"RX_READY impl=\S+ gpio=\d+ inverted=[01]"), timeout=10)
    tx.expect(re.compile(rb"TX_READY impl=\S+ gpio=\d+ inverted=[01]"), timeout=10)
    return tx, rx


def assert_serial_control(tx, rx):
    tx.write("PING\n")
    tx.expect_exact("PONG", timeout=5)
    rx.write("PING\n")
    rx.expect_exact("PONG", timeout=5)


def send_once(tx, case: Case):
    tx.write(f"SEND_AC mode={case.mode} fan={case.fan} temp={case.temp} power={case.power}\n")
    try:
        m = tx.expect(TX_OK_AC, timeout=5)
    except (EOF, TIMEOUT):
        return None
    return m.group("bytes").decode().lower()


def decode_once(rx):
    try:
        m = rx.expect(AC_DECODE, timeout=3)
    except (EOF, TIMEOUT):
        return None
    return {
        "checksum": m.group("checksum").decode(),
        "power": int(m.group("power")),
        "mode": int(m.group("mode")),
        "temp": int(m.group("temp")),
        "fan": int(m.group("fan")),
        "bytes": m.group("bytes").decode().lower(),
    }


def capture_best_of_n(tx, rx, case: Case):
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


@pytest.mark.parametrize("case", CASES, ids=lambda c: f"{c.mode}_{c.fan}_{c.temp}_p{c.power}")
def test_sharp_irremoteesp8266_tx(dut, peers, case, record_property):
    tx, rx = wait_boards_ready(dut, peers)
    assert_serial_control(tx, rx)

    sent, observed, n_ok = capture_best_of_n(tx, rx, case)
    record_property("byte_match_ratio", f"{n_ok}/{TRIALS}")

    if n_ok < PASS_MIN:
        pytest.fail(
            f"{DUT_IMPL} RAW-decoded {case.mode}/{case.fan}/{case.temp}C/power{case.power} to "
            f"canonical bytes only {n_ok}/{TRIALS} times (need >= {PASS_MIN}). "
            f"sent={sent} observed={observed}",
            pytrace=False,
        )

    exp_mode = OUR_MODE[case.mode]
    exp_fan = OUR_FAN[case.fan]
    record_property("peer_impl", PEER_IMPL)
    record_property("dut_impl", DUT_IMPL)
    record_property("sent_state", f"{case.mode}/{case.fan}/{case.temp}C/p{case.power}")
    record_property("canonical_bytes", sent)

    def field(name, got, exp):
        return f"{name}={'ok' if got == exp else f'BAD(exp={exp},got={got})'}"

    # Temp is a don't-care in Auto/Dry (those modes carry no temperature).
    temp_part = field("temp", observed["temp"], case.temp) if case.mode in ("COOL", "HEAT") else "temp=n/a"
    print(
        f"COMPAT_MATRIX_AC_CAL peer={PEER_IMPL} dut={DUT_IMPL} "
        f"sent={case.mode}/{case.fan}/{case.temp}C/p{case.power} byte_match={n_ok}/{TRIALS} "
        f"canonical={sent} {field('power', observed['power'], case.power)} "
        f"{field('mode', observed['mode'], exp_mode)} "
        f"{temp_part} {field('fan', observed['fan'], exp_fan)}"
    )
