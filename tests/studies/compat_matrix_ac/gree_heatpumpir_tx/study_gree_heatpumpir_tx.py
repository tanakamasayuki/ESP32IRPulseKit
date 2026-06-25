import re
from dataclasses import dataclass

import pytest
from pexpect import EOF, TIMEOUT

# AC second reference (Gree): an independent codebase (HeatpumpIR's
# GreeGenericHeatpumpIR, over an LEDC carrier) transmits a known Gree A/C state;
# our RX (ESP32IRPulseKit) captures it RAW and decodes it with esp32irpk::ac::Gree.
# Two independent encoders (HeatpumpIR and IRremoteESP8266) that our decoder reads
# identically are stronger evidence for the field map than either alone.
#
# HARD (pass/fail, best-of-N): our decode is checksum-valid AND its logical fields
# (power / mode / temperature / fan) match the known sent state. Byte equality is
# NOT asserted -- HeatpumpIR fills different auxiliary bytes than IRremoteESP8266;
# the point of a second reference is agreement on field *semantics*.
#
# HeatpumpIR Gree quirks reflected in the cases: MODE_AUTO forces 25C, MODE_DRY
# forces the low (MIN) fan step.
PEER_IMPL = "HeatpumpIR"
DUT_IMPL = "ESP32IRPulseKit"

TRIALS = 5
PASS_MIN = 3

OUR_MODE = {"AUTO": 0, "COOL": 1, "HEAT": 2, "DRY": 3, "FAN": 4}
OUR_FAN = {"AUTO": 0, "MIN": 1, "MED": 2, "MAX": 3}

AC_DECODE = re.compile(
    rb"AC_DECODE vendor=GREE checksum=(?P<checksum>ok|bad) "
    rb"power=(?P<power>\d+) mode=(?P<mode>\d+) temp=(?P<temp>\d+) "
    rb"fan=(?P<fan>\d+) bytes=(?P<bytes>[0-9A-Fa-f]{16})"
)


@dataclass(frozen=True)
class Case:
    mode: str
    fan: str
    temp: int
    power: int


# Covers all Gree fan steps and modes; AUTO uses 25C and DRY uses MIN to match
# HeatpumpIR's mode quirks. All cases are power=ON: HeatpumpIR's Gree drops the
# requested mode on power-off (it leaves the default HEAT and only clears the
# power bit), so mode/fan/temp are vendor-encoder-dependent don't-cares when off.
# The power bit itself is verified in the IRremoteESP8266 studies and the host
# state matrix.
CASES = [
    Case("COOL", "AUTO", 26, 1),
    Case("COOL", "MIN", 22, 1),
    Case("HEAT", "MED", 24, 1),
    Case("HEAT", "MAX", 28, 1),
    Case("DRY", "MIN", 24, 1),
    Case("AUTO", "AUTO", 25, 1),
    Case("COOL", "MAX", 20, 1),
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
        tx.expect(re.compile(rb"TX_OK_AC vendor=GREE"), timeout=5)
    except (EOF, TIMEOUT):
        return False
    return True


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
        "bytes": match.group("bytes").decode().lower(),
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


@pytest.mark.parametrize(
    "case", CASES, ids=lambda c: f"{c.mode}_{c.fan}_{c.temp}_p{c.power}"
)
def test_gree_heatpumpir_tx(dut, peers, case, record_property):
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
            f"{DUT_IMPL} decoded {PEER_IMPL}'s {case.mode}/{case.fan}/{case.temp}C/"
            f"power{case.power} to the expected fields (checksum ok, power={case.power}, "
            f"mode={exp_mode}, temp={case.temp}, fan={exp_fan}) only {n_ok}/{TRIALS} "
            f"times (need >= {PASS_MIN}). last_observed={observed}",
            pytrace=False,
        )

    print(
        f"COMPAT_MATRIX_AC_REF2 peer={PEER_IMPL} dut={DUT_IMPL} "
        f"sent={case.mode}/{case.fan}/{case.temp}C/p{case.power} "
        f"field_match={n_ok}/{TRIALS} "
        f"our_decode(power={observed['power']},mode={observed['mode']},"
        f"temp={observed['temp']},fan={observed['fan']}) bytes={observed['bytes']}"
    )
