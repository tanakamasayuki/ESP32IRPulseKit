import re
from dataclasses import dataclass

import pytest
from pexpect import EOF, TIMEOUT

# AC encoder verification (Fujitsu): our TX (ESP32IRPulseKit, esp32irpk::ac::Fujitsu)
# encodes a known Fujitsu A/C state and transmits it; an INDEPENDENT receiver
# (IRremoteESP8266's IRrecv + IRFujitsuAC) decodes it. This proves our toRaw()
# produces a well-formed ARRAH2E burst that a third-party stack accepts.
#
# HARD (pass/fail, best-of-N): the IRremoteESP8266 decode is checksum-valid AND
# its bytes equal the canonical bytes our peer reports having sent (recovered via
# our own fromRaw, byte-verified by the fujitsu_irremoteesp8266_tx variant).
#
# The peer TX defaults to the phase-aligned carrier (Fujitsu's zero-space 390us <
# bit mark 448us); study_carrier_ab can re-run with the hardware carrier.
PEER_IMPL = "ESP32IRPulseKit"
DUT_IMPL = "IRremoteESP8266"

TRIALS = 5
PASS_MIN = 3

OUR_MODE = {"AUTO": 0, "COOL": 1, "DRY": 2, "FAN": 3, "HEAT": 4}
OUR_FAN = {"AUTO": 0, "HIGH": 1, "MED": 2, "LOW": 3, "QUIET": 4}
OUR_SWING = {"OFF": 0, "VERT": 1, "HORIZ": 2, "BOTH": 3}

# Long frame = 16 bytes (32 hex chars); power-off short frame = 7 bytes (14 chars).
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
def test_fujitsu_irremoteesp8266_rx(dut, peers, case, record_property):
    tx, rx = wait_boards_ready(dut, peers)
    assert_serial_control(tx, rx)

    sent, observed, n_ok = capture_best_of_n(tx, rx, case)
    record_property("byte_match_ratio", f"{n_ok}/{TRIALS}")
    record_property("peer_impl", PEER_IMPL)
    record_property("dut_impl", DUT_IMPL)
    record_property(
        "sent_state", f"{case.mode}/{case.fan}/{case.temp}C/{case.swing}/p{case.power}"
    )
    record_property("canonical_bytes", sent)
    if observed is not None:
        record_property("their_bytes", observed["bytes"])

    # HARD check: an independent decoder must accept our burst and recover the
    # canonical bytes with a valid checksum.
    if n_ok < PASS_MIN:
        pytest.fail(
            f"{DUT_IMPL} decoded {PEER_IMPL}'s {case.mode}/{case.fan}/{case.temp}C/"
            f"{case.swing}/power{case.power} to the canonical bytes only {n_ok}/{TRIALS} "
            f"times (need >= {PASS_MIN}). sent={sent} observed={observed}",
            pytrace=False,
        )

    # SOFT field report (never fails); meaningful only for power-on long frames.
    exp_mode = OUR_MODE[case.mode]
    exp_fan = OUR_FAN[case.fan]
    exp_swing = OUR_SWING[case.swing]

    def field(name, got, exp):
        return f"{name}={'ok' if got == exp else f'BAD(exp={exp},got={got})'}"

    if case.power:
        fields = (
            f"{field('power', observed['power'], case.power)} "
            f"{field('mode', observed['mode'], exp_mode)} "
            f"{field('temp', observed['temp'], case.temp)} "
            f"{field('fan', observed['fan'], exp_fan)} "
            f"{field('swing', observed['swing'], exp_swing)}"
        )
    else:
        fields = field("power", observed["power"], case.power)

    print(
        f"COMPAT_MATRIX_AC_RX peer={PEER_IMPL} dut={DUT_IMPL} "
        f"sent={case.mode}/{case.fan}/{case.temp}C/{case.swing}/p{case.power} "
        f"byte_match={n_ok}/{TRIALS} canonical={sent} {fields}"
    )
