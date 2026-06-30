import re
from dataclasses import dataclass

import pytest
from pexpect import EOF, TIMEOUT

# AC second reference (Fujitsu): an independent codebase (HeatpumpIR's
# FujitsuHeatpumpIR, over an LEDC carrier) transmits a known Fujitsu A/C state;
# our RX (ESP32IRPulseKit) captures it RAW and decodes it with
# esp32irpk::ac::Fujitsu. Two independent encoders (HeatpumpIR and IRremoteESP8266)
# that our decoder reads identically are stronger evidence for the field map than
# either alone.
#
# HARD (pass/fail, best-of-N): our decode is checksum-valid AND its logical fields
# (power / mode / temperature / fan / swing) match the known sent state. Byte
# equality is NOT asserted -- HeatpumpIR fills byte 14 differently from
# IRremoteESP8266; the point is agreement on field *semantics*.
#
# HeatpumpIR's Fujitsu fan constants are inverted vs the wire codes (FAN_1 -> wire
# 4 = quiet .. FAN_4 -> wire 1 = high); the peer maps our tokens to the FAN_x that
# yields the intended wire code, so the expected decoded value is the same numeric
# table as the IRremoteESP8266 study. HeatpumpIR's checksum (0x9E - sum bytes
# 0..14) reduces to our complement of bytes 7..14 for the fixed ARRAH2E header, so
# checksum_ok holds.
PEER_IMPL = "HeatpumpIR"
DUT_IMPL = "ESP32IRPulseKit"

TRIALS = 5
PASS_MIN = 3

OUR_MODE = {"AUTO": 0, "COOL": 1, "DRY": 2, "FAN": 3, "HEAT": 4}
OUR_FAN = {"AUTO": 0, "HIGH": 1, "MED": 2, "LOW": 3, "QUIET": 4}
OUR_SWING = {"OFF": 0, "VERT": 1, "HORIZ": 2, "BOTH": 3}

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


# All cases are power=ON: HeatpumpIR's Fujitsu always emits the full long frame,
# so power-off is not a meaningful second-reference case here (the 7-byte short
# frame is exercised in the IRremoteESP8266 studies and the host state matrix).
# Covers every fan step (incl. QUIET) and each swing axis.
CASES = [
    Case("COOL", "AUTO", 22, "OFF", 1),
    Case("COOL", "LOW", 24, "VERT", 1),
    Case("HEAT", "MED", 26, "HORIZ", 1),
    Case("HEAT", "HIGH", 28, "BOTH", 1),
    Case("DRY", "QUIET", 20, "OFF", 1),
    Case("FAN", "HIGH", 23, "VERT", 1),
    Case("AUTO", "AUTO", 25, "OFF", 1),
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
        tx.expect(re.compile(rb"TX_OK_AC vendor=FUJITSU"), timeout=5)
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
        "swing": int(match.group("swing")),
        "bytes": match.group("bytes").decode().lower(),
    }


def run_trials(tx, rx, case: Case):
    exp_mode = OUR_MODE[case.mode]
    exp_fan = OUR_FAN[case.fan]
    exp_swing = OUR_SWING[case.swing]
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
            and obs["swing"] == exp_swing
        ):
            n_ok += 1
    return obs_last, n_ok


@pytest.mark.parametrize(
    "case", CASES, ids=lambda c: f"{c.mode}_{c.fan}_{c.temp}_{c.swing}_p{c.power}"
)
def test_fujitsu_heatpumpir_tx(dut, peers, case, record_property):
    tx, rx = wait_boards_ready(dut, peers)
    assert_serial_control(tx, rx)

    observed, n_ok = run_trials(tx, rx, case)
    exp_mode = OUR_MODE[case.mode]
    exp_fan = OUR_FAN[case.fan]
    exp_swing = OUR_SWING[case.swing]
    record_property("peer_impl", PEER_IMPL)
    record_property("dut_impl", DUT_IMPL)
    record_property(
        "sent_state", f"{case.mode}/{case.fan}/{case.temp}C/{case.swing}/p{case.power}"
    )
    record_property("field_match_ratio", f"{n_ok}/{TRIALS}")
    if observed is not None:
        record_property("our_bytes", observed["bytes"])

    if n_ok < PASS_MIN:
        pytest.fail(
            f"{DUT_IMPL} decoded {PEER_IMPL}'s {case.mode}/{case.fan}/{case.temp}C/"
            f"{case.swing}/power{case.power} to the expected fields (checksum ok, "
            f"power={case.power}, mode={exp_mode}, temp={case.temp}, fan={exp_fan}, "
            f"swing={exp_swing}) only {n_ok}/{TRIALS} times (need >= {PASS_MIN}). "
            f"last_observed={observed}",
            pytrace=False,
        )

    print(
        f"COMPAT_MATRIX_AC_REF2 peer={PEER_IMPL} dut={DUT_IMPL} "
        f"sent={case.mode}/{case.fan}/{case.temp}C/{case.swing}/p{case.power} "
        f"field_match={n_ok}/{TRIALS} "
        f"our_decode(power={observed['power']},mode={observed['mode']},"
        f"temp={observed['temp']},fan={observed['fan']},swing={observed['swing']}) "
        f"bytes={observed['bytes']}"
    )
