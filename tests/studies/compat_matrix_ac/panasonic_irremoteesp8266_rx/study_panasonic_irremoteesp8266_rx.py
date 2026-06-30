import re
from dataclasses import dataclass

import pytest
from pexpect import EOF, TIMEOUT

# AC encoder verification: our TX (ESP32IRPulseKit's esp32irpk::ac::Panasonic)
# encodes a known Panasonic A/C state and transmits it; an external receiver
# (IRremoteESP8266's IRrecv + IRPanasonicAc) decodes it.
#
# Two independent checks, against the CANONICAL bytes IRremoteESP8266's encoder
# produces for each state (captured in irremoteesp8266_tx, the ground truth --
# NOT our own output, so an encoder bug cannot hide behind a self-comparison):
#   * ENCODER (pass/fail, deterministic): the bytes our encoder emits (the peer
#     echoes them via our own fromRaw) equal the canonical bytes. This is what
#     catches missing/incorrect frame bytes -- e.g. the fixed feature bytes
#     [15]=0x80, [19]=0x0E, [20]=0xE0, [23]=0x81 a real Panasonic frame carries.
#   * TRANSMISSION (best-of-N): the external library decodes our transmitted
#     burst to those canonical bytes with a valid checksum, proving an
#     independent stack accepts what we send over the air.
#
# The external RX also reports IRremoteESP8266's native field values (its own
# mode/fan scale, not ours); these are recorded for inspection, not asserted.
PEER_IMPL = "ESP32IRPulseKit"
DUT_IMPL = "IRremoteESP8266"

TRIALS = 5
PASS_MIN = 3

# The state is always 27 bytes = 54 hex chars. Match exactly 54 so pexpect does
# not lock in a partially-arrived serial buffer (a shorter run would otherwise
# match and truncate the captured bytes).
TX_OK_AC = re.compile(rb"TX_OK_AC vendor=PANASONIC bytes=(?P<bytes>[0-9A-Fa-f]{54})")

AC_DECODE = re.compile(
    rb"AC_DECODE vendor=PANASONIC checksum=(?P<checksum>ok|bad) "
    rb"power=(?P<power>\d+) mode=(?P<mode>\d+) temp=(?P<temp>\d+) "
    rb"fan=(?P<fan>\d+) bytes=(?P<bytes>[0-9A-Fa-f]{54})"
)


@dataclass(frozen=True)
class Case:
    mode: str
    fan: str
    temp: int
    power: int
    # Canonical 27-byte state IRremoteESP8266's IRPanasonicAc encodes for this
    # request (ground truth captured by irremoteesp8266_tx).
    canonical: str


CASES = [
    Case("COOL", "AUTO", 26, 1, "0220e004000000060220e00400313480a000000ee00000810000fa"),
    Case("HEAT", "HIGH", 22, 1, "0220e004000000060220e00400412c806000000ee00000810000c2"),
    Case("DRY", "LOW", 24, 1, "0220e004000000060220e004002130804000000ee0000081000086"),
    Case("COOL", "MED", 18, 1, "0220e004000000060220e004003124805000000ee000008100009a"),
    Case("AUTO", "AUTO", 25, 0, "0220e004000000060220e00400003280a000000ee00000810000c7"),
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
        match = tx.expect(TX_OK_AC, timeout=5)
    except (EOF, TIMEOUT):
        return None
    return match.group("bytes").decode().lower()


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
        "bytes": match.group("bytes").decode().lower(),
    }


def run_trials(tx, rx, case: Case):
    """Send TRIALS times. Return (our_sent, last_observed, n_ok) where n_ok counts
    trials whose external decode equals the canonical bytes with a valid
    checksum. our_sent is our encoder's deterministic output (constant)."""
    n_ok = 0
    our_sent = None
    obs_last = None
    for _ in range(TRIALS):
        sent = send_once(tx, case)
        if sent is None:
            continue
        our_sent = sent
        obs = decode_once(rx)
        if obs is None:
            continue
        obs_last = obs
        if obs["checksum"] == "ok" and obs["bytes"] == case.canonical:
            n_ok += 1
    return our_sent, obs_last, n_ok


@pytest.mark.parametrize(
    "case", CASES, ids=lambda c: f"{c.mode}_{c.fan}_{c.temp}_p{c.power}"
)
def test_irremoteesp8266_rx(dut, peers, case, record_property):
    tx, rx = wait_boards_ready(dut, peers)
    assert_serial_control(tx, rx)

    our_sent, observed, n_ok = run_trials(tx, rx, case)
    record_property("peer_impl", PEER_IMPL)
    record_property("dut_impl", DUT_IMPL)
    record_property("sent_state", f"{case.mode}/{case.fan}/{case.temp}C/p{case.power}")
    record_property("our_encoded_bytes", our_sent)
    record_property("canonical_bytes", case.canonical)
    record_property("canonical_match_ratio", f"{n_ok}/{TRIALS}")

    # ENCODER check (deterministic): our encoder must emit the canonical frame.
    if our_sent is None:
        pytest.fail(f"peer never acknowledged a send for {case.mode}/{case.fan}",
                    pytrace=False)
    if our_sent != case.canonical:
        pytest.fail(
            f"{PEER_IMPL} encoder produced non-canonical bytes for "
            f"{case.mode}/{case.fan}/{case.temp}C/power{case.power}:\n"
            f"  ours      = {our_sent}\n"
            f"  canonical = {case.canonical}",
            pytrace=False,
        )

    # TRANSMISSION check (best-of-N): external lib decodes it to canonical.
    if n_ok < PASS_MIN:
        pytest.fail(
            f"{DUT_IMPL} decoded our transmitted {case.mode}/{case.fan}/"
            f"{case.temp}C/power{case.power} to canonical bytes only {n_ok}/{TRIALS} "
            f"times (need >= {PASS_MIN}). last_observed={observed}",
            pytrace=False,
        )

    # IRremoteESP8266's native field scale (not ours); recorded for inspection.
    record_property("ext_native_power", observed["power"])
    record_property("ext_native_mode", observed["mode"])
    record_property("ext_native_temp", observed["temp"])
    record_property("ext_native_fan", observed["fan"])
    print(
        f"COMPAT_MATRIX_AC_ENC peer={PEER_IMPL} dut={DUT_IMPL} "
        f"sent={case.mode}/{case.fan}/{case.temp}C/p{case.power} "
        f"encoder=canonical canonical_match={n_ok}/{TRIALS} bytes={case.canonical} "
        f"ext_native(power={observed['power']},mode={observed['mode']},"
        f"temp={observed['temp']},fan={observed['fan']})"
    )
