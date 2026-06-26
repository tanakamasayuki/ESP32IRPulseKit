import re
from dataclasses import dataclass

import pytest
from pexpect import EOF, TIMEOUT

# Carrier A/B experiment for Mitsubishi frames. Our TX (peer) sends the SAME known
# Mitsubishi state under each carrier mode; the IRremoteESP8266 primary decodes
# it. We measure the canonical-delivery rate per mode to compare the phase-aligned
# carrier against the free-running hardware carrier.
#
# Mitsubishi's zero-space (420us) is shorter than its bit mark (450us) -- the same
# tight-timing class as Gree -- so the free-running hardware carrier's ~1-cycle
# mark wobble is expected to push spaces out of tolerance and drop frames. This
# study quantifies whether the phase-aligned carrier (exact whole-cycle marks)
# recovers the delivery rate.
#
# Measurement study, not a pass/fail gate: it asserts only that each mode opens
# (CARRIER_OK) and that our encoder still emits the canonical bytes in both modes.
PEER_IMPL = "ESP32IRPulseKit"
DUT_IMPL = "IRremoteESP8266"

CARRIERS = ["hw", "pa"]
TRIALS = 50

TX_OK_AC = re.compile(rb"TX_OK_AC vendor=MITSUBISHI bytes=(?P<bytes>[0-9A-Fa-f]{36})")

AC_DECODE = re.compile(
    rb"AC_DECODE vendor=MITSUBISHI checksum=(?P<checksum>ok|bad) "
    rb"power=(?P<power>\d+) mode=(?P<mode>\d+) temp=(?P<temp>\d+) "
    rb"fan=(?P<fan>\d+) bytes=(?P<bytes>[0-9A-Fa-f]{36})"
)


@dataclass(frozen=True)
class MitsubishiCase:
    mode: str
    fan: str
    temp: int
    power: int
    canonical: str


# A few representative states; canonical bytes confirmed by
# mitsubishi_irremoteesp8266_tx.
CASES = [
    MitsubishiCase("COOL", "AUTO", 22, 1, "23cb26010020180636c067000000000000b0"),
    MitsubishiCase("HEAT", "MAX", 24, 1, "23cb26010020080830446700000000000020"),
    MitsubishiCase("FAN", "MED", 28, 1, "23cb26010020380c37426700000000000059"),
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


def send_once(tx, case: MitsubishiCase):
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
        match = rx.expect(AC_DECODE, timeout=12)
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
def test_mitsubishi_carrier_ab(dut, peers, case, carrier, record_property):
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
