import re
from dataclasses import dataclass

import pytest
from pexpect import EOF, TIMEOUT

# AC decoder calibration (TCL): an INDEPENDENT transmitter (IRremoteESP8266's
# IRTcl112Ac) encodes a known 14-byte TCL112AC state and transmits it; our RX
# (ESP32IRPulseKit, esp32irpk::ac::Tcl) captures and decodes it. Proves our
# fromRaw() reads a third-party TCL burst and recovers the same bytes + fields.
#
# HARD (pass/fail, best-of-N): our decode is checksum-valid AND its bytes equal the
# canonical bytes the peer reports having sent.
#
# Both stacks use the same field codes (mode: heat1/dry2/cool3/fan7/auto8;
# fan: auto0/min1/low2/med3/high5). Temperature is whole degrees here (16-31 C).
PEER_IMPL = "IRremoteESP8266"
DUT_IMPL = "ESP32IRPulseKit"

TRIALS = 5
PASS_MIN = 3

OUR_MODE = {"HEAT": 1, "DRY": 2, "COOL": 3, "FAN": 7, "AUTO": 8}
OUR_FAN = {"AUTO": 0, "MIN": 1, "LOW": 2, "MED": 3, "HIGH": 5}

# 14 bytes = 28 hex chars. The trailing newline anchor is required so pexpect waits
# for the whole line instead of matching a partial hex prefix from the first chunk.
TX_OK_AC = re.compile(rb"TX_OK_AC vendor=TCL bytes=(?P<bytes>[0-9A-Fa-f]{28})\r?\n")
AC_DECODE = re.compile(
    rb"AC_DECODE vendor=TCL checksum=(?P<checksum>ok|bad) "
    rb"power=(?P<power>\d+) mode=(?P<mode>\d+) temp=(?P<temp>\d+) "
    rb"fan=(?P<fan>\d+) bytes=(?P<bytes>[0-9A-Fa-f]{28})\r?\n"
)


@dataclass(frozen=True)
class Case:
    mode: str
    fan: str
    temp: int
    power: int


CASES = [
    Case("COOL", "HIGH", 24, 1),
    Case("HEAT", "MED", 28, 1),
    Case("AUTO", "AUTO", 25, 1),
    Case("DRY", "LOW", 22, 1),
    Case("FAN", "HIGH", 30, 1),
    Case("COOL", "AUTO", 16, 0),
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
def test_tcl_irremoteesp8266_tx(dut, peers, case, record_property):
    tx, rx = wait_boards_ready(dut, peers)
    assert_serial_control(tx, rx)

    sent, observed, n_ok = capture_best_of_n(tx, rx, case)
    record_property("byte_match_ratio", f"{n_ok}/{TRIALS}")
    record_property("peer_impl", PEER_IMPL)
    record_property("dut_impl", DUT_IMPL)
    record_property("sent_state", f"{case.mode}/{case.fan}/{case.temp}C/p{case.power}")
    record_property("canonical_bytes", sent)
    if observed is not None:
        record_property("their_bytes", observed["bytes"])

    if n_ok < PASS_MIN:
        pytest.fail(
            f"{DUT_IMPL} decoded {PEER_IMPL}'s {case.mode}/{case.fan}/{case.temp}C/power{case.power} "
            f"to the canonical bytes only {n_ok}/{TRIALS} times (need >= {PASS_MIN}). "
            f"sent={sent} observed={observed}",
            pytrace=False,
        )

    exp_mode = OUR_MODE[case.mode]
    exp_fan = OUR_FAN[case.fan]

    def field(name, got, exp):
        return f"{name}={'ok' if got == exp else f'BAD(exp={exp},got={got})'}"

    print(
        f"COMPAT_MATRIX_AC_TX peer={PEER_IMPL} dut={DUT_IMPL} "
        f"sent={case.mode}/{case.fan}/{case.temp}C/p{case.power} byte_match={n_ok}/{TRIALS} "
        f"canonical={sent} {field('power', observed['power'], case.power)} "
        f"{field('mode', observed['mode'], exp_mode)} "
        f"{field('temp', observed['temp'], case.temp)} {field('fan', observed['fan'], exp_fan)}"
    )
