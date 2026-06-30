import re
from dataclasses import dataclass

import pytest
from pexpect import EOF, TIMEOUT

# AC second reference (Daikin): an independent codebase (HeatpumpIR's
# DaikinHeatpumpIR, over an LEDC carrier) transmits a known Daikin A/C state
# (classic ARC433); our RX (ESP32IRPulseKit) captures it RAW and decodes it with
# esp32irpk::ac::Daikin. Two independent encoders (HeatpumpIR and IRremoteESP8266)
# our decoder reads identically are stronger evidence for the field map than
# either alone.
#
# HARD (pass/fail, best-of-N): our decode is checksum-valid AND its calibrated
# fields (power / mode / fan, plus temperature in modes that honour it) match the
# known sent state. Byte identity is NOT asserted -- HeatpumpIR fills some auxiliary
# bytes differently. Swing is recorded, not asserted: HeatpumpIR's Daikin encoder
# does not drive swing (it stays off). Temperature is a don't-care in DRY/FAN:
# HeatpumpIR leaves byte 22 at its template value (0x17) in those modes, so temp is
# only asserted in AUTO/COOL/HEAT.
TEMP_HONORED = {"AUTO", "COOL", "HEAT"}
#
# HeatpumpIR's Daikin mode constants already sit in byte-21 bits 4-6 and its fan
# constants in byte-24's high nibble, so the expected decoded values are the same
# numeric table as the IRremoteESP8266 study (minus quiet, which HeatpumpIR's
# Daikin lacks). HeatpumpIR keeps the template's section-1/2 checksums and only
# recomputes byte 34, so our three-checksum validation holds.
PEER_IMPL = "HeatpumpIR"
DUT_IMPL = "ESP32IRPulseKit"

TRIALS = 5
PASS_MIN = 3

OUR_MODE = {"AUTO": 0, "DRY": 2, "COOL": 3, "HEAT": 4, "FAN": 6}
OUR_FAN = {"AUTO": 10, "MIN": 3, "LOW": 4, "MED": 5, "HIGH": 6, "MAX": 7}

AC_DECODE = re.compile(
    rb"AC_DECODE vendor=DAIKIN checksum=(?P<checksum>ok|bad) "
    rb"power=(?P<power>\d+) mode=(?P<mode>\d+) temp=(?P<temp>\d+) "
    rb"fan=(?P<fan>\d+) swingv=(?P<swingv>\d+) swingh=(?P<swingh>\d+) "
    rb"bytes=(?P<bytes>[0-9A-Fa-f]{70})\r?\n"
)


@dataclass(frozen=True)
class Case:
    mode: str
    fan: str
    temp: int
    power: int


# All power=ON (the heatpumpir-study convention). Covers every fan step HeatpumpIR
# Daikin supports (no quiet) and each mode.
CASES = [
    Case("COOL", "AUTO", 25, 1),
    Case("HEAT", "MAX", 28, 1),
    Case("DRY", "MIN", 22, 1),
    Case("FAN", "MED", 26, 1),
    Case("COOL", "HIGH", 20, 1),
    Case("COOL", "LOW", 23, 1),
    Case("AUTO", "AUTO", 24, 1),
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
        f"SEND_AC mode={case.mode} fan={case.fan} temp={case.temp} power={case.power}\n"
    )
    try:
        tx.expect(re.compile(rb"TX_OK_AC vendor=DAIKIN"), timeout=5)
    except (EOF, TIMEOUT):
        return False
    return True


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
        "swingv": int(match.group("swingv")),
        "swingh": int(match.group("swingh")),
        "bytes": match.group("bytes").decode().lower(),
    }


def run_trials(tx, rx, case: Case):
    exp_mode = OUR_MODE[case.mode]
    exp_fan = OUR_FAN[case.fan]
    check_temp = case.mode in TEMP_HONORED
    n_ok = 0
    obs_last = None
    for _ in range(TRIALS):
        if not send_once(tx, case):
            continue
        obs = decode_once(rx)
        if obs is None:
            continue
        obs_last = obs
        if (
            obs["checksum"] == "ok"
            and obs["power"] == case.power
            and obs["mode"] == exp_mode
            and obs["fan"] == exp_fan
            and (not check_temp or obs["temp"] == case.temp)
        ):
            n_ok += 1
    return obs_last, n_ok


@pytest.mark.parametrize(
    "case", CASES, ids=lambda c: f"{c.mode}_{c.fan}_{c.temp}_p{c.power}"
)
def test_daikin_heatpumpir_tx(dut, peers, case, record_property):
    tx, rx = wait_boards_ready(dut, peers)
    assert_serial_control(tx, rx)

    observed, n_ok = run_trials(tx, rx, case)
    exp_mode = OUR_MODE[case.mode]
    exp_fan = OUR_FAN[case.fan]
    record_property("peer_impl", PEER_IMPL)
    record_property("dut_impl", DUT_IMPL)
    record_property("sent_state", f"{case.mode}/{case.fan}/{case.temp}C/p{case.power}")
    record_property("field_match_ratio", f"{n_ok}/{TRIALS}")
    if observed is not None:
        record_property("our_bytes", observed["bytes"])

    if n_ok < PASS_MIN:
        temp_clause = f"temp={case.temp}, " if case.mode in TEMP_HONORED else "temp=ignored, "
        pytest.fail(
            f"{DUT_IMPL} decoded {PEER_IMPL}'s {case.mode}/{case.fan}/{case.temp}C/"
            f"power{case.power} to the expected fields (checksum ok, power={case.power}, "
            f"mode={exp_mode}, {temp_clause}fan={exp_fan}) only {n_ok}/{TRIALS} "
            f"times (need >= {PASS_MIN}). last_observed={observed}",
            pytrace=False,
        )

    print(
        f"COMPAT_MATRIX_AC_REF2 peer={PEER_IMPL} dut={DUT_IMPL} "
        f"sent={case.mode}/{case.fan}/{case.temp}C/p{case.power} "
        f"field_match={n_ok}/{TRIALS} "
        f"our_decode(power={observed['power']},mode={observed['mode']},"
        f"temp={observed['temp']},fan={observed['fan']},swingv={observed['swingv']},"
        f"swingh={observed['swingh']}) bytes={observed['bytes']}"
    )
