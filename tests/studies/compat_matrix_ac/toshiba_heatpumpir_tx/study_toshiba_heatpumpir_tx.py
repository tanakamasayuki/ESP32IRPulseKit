import re
from dataclasses import dataclass

import pytest
from pexpect import EOF, TIMEOUT

# AC second reference (Toshiba): an independent codebase (HeatpumpIR's
# ToshibaHeatpumpIR, over an LEDC carrier) transmits a known Toshiba A/C state; our
# RX captures it RAW and decodes it with esp32irpk::ac::Toshiba. Two independent
# encoders (HeatpumpIR and IRremoteESP8266) that our decoder reads identically are
# stronger evidence for the field map than either alone.
#
# HARD (pass/fail, best-of-N): our decode is checksum-valid AND power/mode/temp/fan
# match the known state. Byte identity is not asserted. All cases are power=ON.
#
# HeatpumpIR stores the state bit-reversed; on the wire it is the standard MSB-first
# TOSHIBA_AC frame. Its modes reduce to wire codes auto=0/cool=1/dry=2/heat=3 (no
# FAN mode) and FAN1..FAN5 to wire fan fields 2..6 (our MIN..MAX); auto=0. So the
# expected decoded values use the same numeric table as the IRremoteESP8266 study
# (minus the FAN operating mode).
PEER_IMPL = "HeatpumpIR"
DUT_IMPL = "ESP32IRPulseKit"

TRIALS = 5
PASS_MIN = 3

OUR_MODE = {"AUTO": 0, "COOL": 1, "DRY": 2, "HEAT": 3}
OUR_FAN = {"AUTO": 0, "MIN": 2, "LOW": 3, "MED": 4, "HIGH": 5, "MAX": 6}

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


# power=ON only (heatpumpir-study convention). Modes auto/cool/dry/heat (HeatpumpIR
# Toshiba has no FAN mode); every fan step.
CASES = [
    Case("COOL", "AUTO", 24, 1),
    Case("HEAT", "MAX", 28, 1),
    Case("DRY", "MIN", 20, 1),
    Case("COOL", "MED", 26, 1),
    Case("COOL", "LOW", 18, 1),
    Case("HEAT", "HIGH", 30, 1),
    Case("AUTO", "AUTO", 22, 1),
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
        tx.expect(re.compile(rb"TX_OK_AC vendor=TOSHIBA"), timeout=5)
    except (EOF, TIMEOUT):
        return False
    return True


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


def run_trials(tx, rx, case: Case):
    exp_mode = OUR_MODE[case.mode]
    exp_fan = OUR_FAN[case.fan]
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
            and obs["temp"] == case.temp
            and obs["fan"] == exp_fan
        ):
            n_ok += 1
    return obs_last, n_ok


@pytest.mark.parametrize("case", CASES, ids=lambda c: f"{c.mode}_{c.fan}_{c.temp}_p{c.power}")
def test_toshiba_heatpumpir_tx(dut, peers, case, record_property):
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
        pytest.fail(
            f"{DUT_IMPL} decoded {PEER_IMPL}'s {case.mode}/{case.fan}/{case.temp}C/power{case.power} "
            f"to the expected fields (checksum ok, power={case.power}, mode={exp_mode}, "
            f"temp={case.temp}, fan={exp_fan}) only {n_ok}/{TRIALS} times (need >= {PASS_MIN}). "
            f"last_observed={observed}",
            pytrace=False,
        )

    print(
        f"COMPAT_MATRIX_AC_REF2 peer={PEER_IMPL} dut={DUT_IMPL} "
        f"sent={case.mode}/{case.fan}/{case.temp}C/p{case.power} field_match={n_ok}/{TRIALS} "
        f"our_decode(power={observed['power']},mode={observed['mode']},temp={observed['temp']},"
        f"fan={observed['fan']}) bytes={observed['bytes']}"
    )
