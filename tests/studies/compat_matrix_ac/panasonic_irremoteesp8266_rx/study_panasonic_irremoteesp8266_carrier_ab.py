import re
from dataclasses import dataclass

import pytest
from pexpect import EOF, TIMEOUT

# Carrier A/B experiment for long AC frames. Our TX (peer) sends the SAME known
# Panasonic state under each carrier mode; the IRremoteESP8266 primary decodes
# it. We measure the canonical-delivery rate per mode to compare the
# phase-aligned carrier against the free-running hardware carrier for frames this
# long (SPEC 11.3: phase-aligned explodes the symbol count and raises underrun
# risk -- this quantifies whether that actually hurts delivery).
#
# This is a measurement study, not a pass/fail gate: it asserts only that each
# mode opens (CARRIER_OK) and that our encoder still emits the canonical bytes in
# both modes. The delivery ratio per mode is recorded and printed for comparison;
# a degraded phase-aligned rate is the finding, not a failure.
PEER_IMPL = "ESP32IRPulseKit"
DUT_IMPL = "IRremoteESP8266"

CARRIERS = ["hw", "pa"]
TRIALS = 50

TX_OK_AC = re.compile(rb"TX_OK_AC vendor=PANASONIC bytes=(?P<bytes>[0-9A-Fa-f]{54})")

AC_DECODE = re.compile(
    rb"AC_DECODE vendor=PANASONIC checksum=(?P<checksum>ok|bad) "
    rb"power=(?P<power>\d+) mode=(?P<mode>\d+) temp=(?P<temp>\d+) "
    rb"fan=(?P<fan>\d+) bytes=(?P<bytes>[0-9A-Fa-f]{54})"
)


@dataclass(frozen=True)
class PanasonicCase:
    mode: str
    fan: str
    temp: int
    power: int
    canonical: str


# A few representative states; canonical bytes are the ground truth captured by
# irremoteesp8266_tx.
CASES = [
    PanasonicCase("COOL", "AUTO", 26, 1, "0220e004000000060220e00400313480a000000ee00000810000fa"),
    PanasonicCase("HEAT", "HIGH", 22, 1, "0220e004000000060220e00400412c806000000ee00000810000c2"),
    PanasonicCase("COOL", "MED", 18, 1, "0220e004000000060220e004003124805000000ee000008100009a"),
]


def wait_boards_ready(dut, peers):
    tx = peers["tx"]
    rx = dut
    rx.write("READY\n")
    tx.write("READY\n")
    rx.expect(re.compile(rb"RX_READY impl=\S+ gpio=\d+ inverted=[01]"), timeout=20)
    tx.expect(re.compile(rb"TX_READY impl=\S+ gpio=\d+ inverted=[01]"), timeout=20)
    return tx, rx


def set_carrier(tx, mode: str):
    tx.write(f"CARRIER {mode}\n")
    tx.expect_exact(f"CARRIER_OK mode={mode}", timeout=10)


def send_once(tx, case: PanasonicCase):
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
        "bytes": match.group("bytes").decode().lower(),
    }


@pytest.mark.parametrize("carrier", CARRIERS)
@pytest.mark.parametrize(
    "case", CASES, ids=lambda c: f"{c.mode}_{c.fan}_{c.temp}_p{c.power}"
)
def test_irremoteesp8266_carrier_ab(dut, peers, case, carrier, record_property):
    tx, rx = wait_boards_ready(dut, peers)
    set_carrier(tx, carrier)

    n_deliver = 0
    our_sent = None
    for _ in range(TRIALS):
        sent = send_once(tx, case)
        if sent is None:
            continue
        our_sent = sent
        obs = decode_once(rx)
        if obs and obs["checksum"] == "ok" and obs["bytes"] == case.canonical:
            n_deliver += 1

    # Leave the sender on the hardware carrier so a later test on the same board
    # is not affected by this experiment's last mode.
    if carrier != "hw":
        set_carrier(tx, "hw")

    record_property("carrier", carrier)
    record_property("sent_state", f"{case.mode}/{case.fan}/{case.temp}C/p{case.power}")
    record_property("delivery_ratio", f"{n_deliver}/{TRIALS}")
    record_property("our_encoded_bytes", our_sent)
    print(
        f"COMPAT_MATRIX_AC_CARRIER carrier={carrier} "
        f"state={case.mode}/{case.fan}/{case.temp}C/p{case.power} "
        f"delivery={n_deliver}/{TRIALS} our_bytes={our_sent}"
    )

    # Measurement study: the only hard checks are that the mode opened (CARRIER_OK
    # above) and that our encoder is carrier-independent and still canonical.
    assert our_sent is not None, f"peer never acknowledged a send on carrier={carrier}"
    assert our_sent == case.canonical, (
        f"encoder bytes changed on carrier={carrier}: {our_sent} != {case.canonical}"
    )
