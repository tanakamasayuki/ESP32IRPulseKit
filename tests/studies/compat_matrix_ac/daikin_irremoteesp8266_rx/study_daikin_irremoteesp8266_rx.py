import re
from dataclasses import dataclass

import pytest
from pexpect import EOF, TIMEOUT

# AC encoder verification (Daikin): our TX (ESP32IRPulseKit, esp32irpk::ac::Daikin)
# encodes a known Daikin A/C state (classic ARC433) and transmits it; an
# INDEPENDENT receiver (IRremoteESP8266's IRrecv + IRDaikinESP) decodes it. This
# proves our toRaw() -- the 5-bit preamble plus the three sections with per-section
# checksums -- produces a burst a third-party stack accepts.
#
# HARD (pass/fail, best-of-N): the IRremoteESP8266 decode is checksum-valid AND its
# bytes equal the canonical bytes our peer reports having sent (recovered via our
# own fromRaw, byte-verified by the daikin_irremoteesp8266_tx variant).
#
# The peer TX defaults to the phase-aligned carrier (Daikin's zero-space 428us ==
# bit mark 428us, the tightest of any vendor).
PEER_IMPL = "ESP32IRPulseKit"
DUT_IMPL = "IRremoteESP8266"

TRIALS = 5
PASS_MIN = 3

OUR_MODE = {"AUTO": 0, "DRY": 2, "COOL": 3, "HEAT": 4, "FAN": 6}
OUR_FAN = {"AUTO": 10, "QUIET": 11, "MIN": 3, "LOW": 4, "MED": 5, "HIGH": 6, "MAX": 7}

TX_OK_AC = re.compile(rb"TX_OK_AC vendor=DAIKIN bytes=(?P<bytes>[0-9A-Fa-f]{70})\r?\n")

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
    swingv: int
    swingh: int
    power: int


CASES = [
    Case("COOL", "AUTO", 25, 0, 0, 1),
    Case("HEAT", "MAX", 28, 1, 0, 1),
    Case("DRY", "MIN", 22, 0, 1, 1),
    Case("FAN", "MED", 26, 1, 1, 1),
    Case("COOL", "QUIET", 18, 0, 0, 1),
    Case("AUTO", "AUTO", 24, 0, 0, 0),
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
        f"swingv={case.swingv} swingh={case.swingh} power={case.power}\n"
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
        "swingv": int(match.group("swingv")),
        "swingh": int(match.group("swingh")),
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
    "case",
    CASES,
    ids=lambda c: f"{c.mode}_{c.fan}_{c.temp}_v{c.swingv}h{c.swingh}_p{c.power}",
)
def test_daikin_irremoteesp8266_rx(dut, peers, case, record_property):
    tx, rx = wait_boards_ready(dut, peers)
    assert_serial_control(tx, rx)

    sent, observed, n_ok = capture_best_of_n(tx, rx, case)
    record_property("byte_match_ratio", f"{n_ok}/{TRIALS}")
    record_property("peer_impl", PEER_IMPL)
    record_property("dut_impl", DUT_IMPL)
    record_property(
        "sent_state",
        f"{case.mode}/{case.fan}/{case.temp}C/v{case.swingv}h{case.swingh}/p{case.power}",
    )
    record_property("canonical_bytes", sent)
    if observed is not None:
        record_property("their_bytes", observed["bytes"])

    # HARD check: an independent decoder must accept our burst and recover the
    # canonical bytes with valid section checksums.
    if n_ok < PASS_MIN:
        pytest.fail(
            f"{DUT_IMPL} decoded {PEER_IMPL}'s {case.mode}/{case.fan}/{case.temp}C/"
            f"v{case.swingv}h{case.swingh}/power{case.power} to the canonical bytes only "
            f"{n_ok}/{TRIALS} times (need >= {PASS_MIN}). sent={sent} observed={observed}",
            pytrace=False,
        )

    exp_mode = OUR_MODE[case.mode]
    exp_fan = OUR_FAN[case.fan]

    def field(name, got, exp):
        return f"{name}={'ok' if got == exp else f'BAD(exp={exp},got={got})'}"

    print(
        f"COMPAT_MATRIX_AC_RX peer={PEER_IMPL} dut={DUT_IMPL} "
        f"sent={case.mode}/{case.fan}/{case.temp}C/v{case.swingv}h{case.swingh}/p{case.power} "
        f"byte_match={n_ok}/{TRIALS} canonical={sent} "
        f"{field('power', observed['power'], case.power)} "
        f"{field('mode', observed['mode'], exp_mode)} "
        f"{field('temp', observed['temp'], case.temp)} "
        f"{field('fan', observed['fan'], exp_fan)} "
        f"{field('swingv', observed['swingv'], case.swingv)} "
        f"{field('swingh', observed['swingh'], case.swingh)}"
    )
