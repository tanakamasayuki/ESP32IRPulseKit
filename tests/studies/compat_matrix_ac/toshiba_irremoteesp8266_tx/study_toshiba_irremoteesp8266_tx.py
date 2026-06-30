import re
from dataclasses import dataclass

import pytest
from pexpect import EOF, TIMEOUT

# AC field-map calibration (Toshiba): IRremoteESP8266's IRToshibaAC (standard
# 9-byte TOSHIBA_AC) transmits a KNOWN state; our RX captures it RAW and decodes it
# with esp32irpk::ac::Toshiba.
#
#   * HARD (pass/fail): our recovered 9 bytes equal the peer's canonical 9 bytes,
#     with a valid XOR checksum. Proves our RAW capture + MSB-first framing.
#   * SOFT (recorded): our decoded fields vs the known state (calibration). Mode is
#     only meaningful when powered on (power off masks the Mode field as 7).
#
# Our enum underlying values equal the wire codes (Mode auto=0/cool=1/dry=2/heat=3/
# fan=4; Fan auto=0, then 2..6 for min..max), so the comparison is a direct match.
PEER_IMPL = "IRremoteESP8266"
DUT_IMPL = "ESP32IRPulseKit"

TRIALS = 5
PASS_MIN = 3

OUR_MODE = {"AUTO": 0, "COOL": 1, "DRY": 2, "HEAT": 3, "FAN": 4}
OUR_FAN = {"AUTO": 0, "MIN": 2, "LOW": 3, "MED": 4, "HIGH": 5, "MAX": 6}

# 9 bytes = 18 hex chars.
TX_OK_AC = re.compile(rb"TX_OK_AC vendor=TOSHIBA bytes=(?P<bytes>[0-9A-Fa-f]{18})\r?\n")
AC_DECODE = re.compile(
    rb"AC_DECODE vendor=TOSHIBA checksum=(?P<checksum>ok|bad) "
    rb"power=(?P<power>\d+) mode=(?P<mode>\d+) temp=(?P<temp>\d+) "
    rb"fan=(?P<fan>\d+) bytes=(?P<bytes>[0-9A-Fa-f]{18})\r?\n"
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
    Case("DRY", "MIN", 20, 1),
    Case("FAN", "MED", 26, 1),
    Case("COOL", "LOW", 18, 1),
    Case("AUTO", "AUTO", 22, 0),
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
def test_toshiba_irremoteesp8266_tx(dut, peers, case, record_property):
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

    # Mode is a don't-care when powered off (Mode field == 7).
    mode_part = field("mode", observed["mode"], exp_mode) if case.power else "mode=off"
    print(
        f"COMPAT_MATRIX_AC_CAL peer={PEER_IMPL} dut={DUT_IMPL} "
        f"sent={case.mode}/{case.fan}/{case.temp}C/p{case.power} byte_match={n_ok}/{TRIALS} "
        f"canonical={sent} {field('power', observed['power'], case.power)} {mode_part} "
        f"{field('temp', observed['temp'], case.temp)} {field('fan', observed['fan'], exp_fan)}"
    )
