import re
from dataclasses import dataclass

import pytest
from pexpect import EOF, TIMEOUT

# AC field-map calibration (Daikin): an external transmitter (IRremoteESP8266's
# IRDaikinESP, classic ARC433) sends a KNOWN Daikin A/C state; our RX
# (ESP32IRPulseKit) captures it RAW and decodes it with esp32irpk::ac::Daikin.
#
# Two things are checked per case:
#   * HARD (pass/fail): our recovered 35 bytes equal the peer's canonical 35 bytes,
#     with all three section checksums valid. This proves our RAW capture, the
#     5-bit preamble skip, and the three-section framing are correct.
#   * SOFT (recorded + printed, never fails): our decoded fields vs the known sent
#     state. A field mismatch means the field map in src/ac/Daikin.h needs work.
#
# Our esp32irpk::ac::Daikin enum underlying values equal the IRremoteESP8266 wire
# codes (Mode auto=0/dry=2/cool=3/heat=4/fan=6; Fan auto=0x0A/quiet=0x0B/3..7), so
# the SOFT comparison is a direct numeric match.
PEER_IMPL = "IRremoteESP8266"
DUT_IMPL = "ESP32IRPulseKit"

TRIALS = 5
PASS_MIN = 3

OUR_MODE = {"AUTO": 0, "DRY": 2, "COOL": 3, "HEAT": 4, "FAN": 6}
OUR_FAN = {"AUTO": 10, "QUIET": 11, "MIN": 3, "LOW": 4, "MED": 5, "HIGH": 6, "MAX": 7}

# State is 35 bytes = 70 hex chars. Anchor on the trailing newline so a partial
# buffer is not matched mid-line.
TX_OK_AC = re.compile(rb"TX_OK_AC vendor=DAIKIN bytes=(?P<bytes>[0-9A-Fa-f]{70})\r?\n")

AC_DECODE = re.compile(
    rb"AC_DECODE vendor=DAIKIN checksum=(?P<checksum>ok|bad) "
    rb"power=(?P<power>\d+) mode=(?P<mode>\d+) temp=(?P<temp>\d+) "
    rb"fan=(?P<fan>\d+) swingv=(?P<swingv>\d+) swingh=(?P<swingh>\d+) "
    rb"bytes=(?P<bytes>[0-9A-Fa-f]{70})\r?\n"
)

# Decode-failure diagnostic from the RX sketch: the captured symbol count and the
# durations (us). Surfaced in the failure message so a bad capture (fragmented vs
# full ~584-symbol burst, or timing skew) can be diagnosed without a side channel.
AC_RAW = re.compile(
    rb"AC_RAW vendor=NONE raw_len=(?P<len>\d+) ticks_us=(?P<ticks>[\d,]+)\r?\n"
)


@dataclass(frozen=True)
class Case:
    mode: str
    fan: str
    temp: int
    swingv: int
    swingh: int
    power: int


# Spread of distinct states so the calibration table covers several field values.
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


# Returns ("decode", {...}) on a successful AC decode, ("raw", "<len> ...") with the
# capture diagnostic when our decoder rejected the burst, or (None, None) on timeout.
def decode_once(rx):
    try:
        idx = rx.expect([AC_DECODE, AC_RAW, TIMEOUT, EOF], timeout=12)
    except (EOF, TIMEOUT):
        return None, None
    if idx == 1:
        m = rx.match
        return "raw", f"len={m.group('len').decode()} ticks_us={m.group('ticks').decode()}"
    if idx >= 2:
        return None, None
    m = rx.match
    return "decode", {
        "checksum": m.group("checksum").decode(),
        "power": int(m.group("power")),
        "mode": int(m.group("mode")),
        "temp": int(m.group("temp")),
        "fan": int(m.group("fan")),
        "swingv": int(m.group("swingv")),
        "swingh": int(m.group("swingh")),
        "bytes": m.group("bytes").decode().lower(),
    }


def capture_best_of_n(tx, rx, case: Case):
    n_ok = 0
    sent_last = None
    obs_last = None
    raw_last = None
    for _ in range(TRIALS):
        sent = send_once(tx, case)
        if sent is None:
            continue
        sent_last = sent
        kind, payload = decode_once(rx)
        if kind == "raw":
            raw_last = payload
            continue
        if kind != "decode":
            continue
        obs_last = payload
        if payload["checksum"] == "ok" and payload["bytes"] == sent:
            n_ok += 1
    return sent_last, obs_last, n_ok, raw_last


@pytest.mark.parametrize(
    "case",
    CASES,
    ids=lambda c: f"{c.mode}_{c.fan}_{c.temp}_v{c.swingv}h{c.swingh}_p{c.power}",
)
def test_daikin_irremoteesp8266_tx(dut, peers, case, record_property):
    tx, rx = wait_boards_ready(dut, peers)
    assert_serial_control(tx, rx)

    sent, observed, n_ok, raw_diag = capture_best_of_n(tx, rx, case)
    record_property("byte_match_ratio", f"{n_ok}/{TRIALS}")
    if raw_diag is not None:
        record_property("rx_capture", raw_diag)

    # HARD check: our RAW framing must reproduce the canonical bytes.
    if n_ok < PASS_MIN:
        pytest.fail(
            f"{DUT_IMPL} RAW-decoded {case.mode}/{case.fan}/{case.temp}C/"
            f"v{case.swingv}h{case.swingh}/power{case.power} to canonical bytes only "
            f"{n_ok}/{TRIALS} times (need >= {PASS_MIN}). sent={sent} observed={observed} "
            f"rx_capture[{raw_diag}]",
            pytrace=False,
        )

    # SOFT check: field-map calibration. Reported, never fails.
    exp_mode = OUR_MODE[case.mode]
    exp_fan = OUR_FAN[case.fan]
    record_property("peer_impl", PEER_IMPL)
    record_property("dut_impl", DUT_IMPL)
    record_property(
        "sent_state",
        f"{case.mode}/{case.fan}/{case.temp}C/v{case.swingv}h{case.swingh}/p{case.power}",
    )
    record_property("canonical_bytes", sent)
    record_property("our_mode", observed["mode"])
    record_property("our_temp", observed["temp"])
    record_property("our_fan", observed["fan"])
    record_property("our_swingv", observed["swingv"])
    record_property("our_swingh", observed["swingh"])
    record_property("our_power", observed["power"])

    def field(name, got, exp):
        return f"{name}={'ok' if got == exp else f'BAD(exp={exp},got={got})'}"

    print(
        f"COMPAT_MATRIX_AC_CAL peer={PEER_IMPL} dut={DUT_IMPL} "
        f"sent={case.mode}/{case.fan}/{case.temp}C/v{case.swingv}h{case.swingh}/p{case.power} "
        f"byte_match={n_ok}/{TRIALS} canonical={sent} "
        f"{field('power', observed['power'], case.power)} "
        f"{field('mode', observed['mode'], exp_mode)} "
        f"{field('temp', observed['temp'], case.temp)} "
        f"{field('fan', observed['fan'], exp_fan)} "
        f"{field('swingv', observed['swingv'], case.swingv)} "
        f"{field('swingh', observed['swingh'], case.swingh)}"
    )
