from pathlib import Path
import subprocess
import sys

import pytest
import yaml


FIXTURE_DIR = Path(__file__).with_name("verified")
CPP_HEADER = Path(__file__).resolve().parents[1] / "host" / "codec_smoke" / "verified_fixtures.h"
CPP_EXPORTER = Path(__file__).with_name("export_cpp_fixtures.py")


def load_fixture(name: str):
    with (FIXTURE_DIR / name).open(encoding="utf-8") as fh:
        return yaml.safe_load(fh)


def nec_bits(address: int, command: int) -> int:
    return address | (command << 16) | (((~command) & 0xFF) << 24)


def nec_raw_ticks(address: int, command: int) -> list[int]:
    bits = nec_bits(address, command)
    raw = [900, 450]
    for bit_index in range(32):
        raw.append(56)
        raw.append(169 if ((bits >> bit_index) & 0x1) else 56)
    raw.append(56)
    return raw


def sony12_raw_ticks(data: int) -> list[int]:
    raw = [240, 60]
    for bit_index in range(12):
        raw.append(120 if ((data >> bit_index) & 0x1) else 60)
        raw.append(60)
    return raw


def samsung32_bits(address: int, command: int) -> int:
    return address | (command << 16)


def samsung32_raw_ticks(address: int, command: int) -> list[int]:
    bits = samsung32_bits(address, command)
    raw = [450, 450]
    for bit_index in range(32):
        raw.append(56)
        raw.append(169 if ((bits >> bit_index) & 0x1) else 56)
    raw.append(56)
    return raw


def aeha_raw_ticks(data: int, bit_length: int) -> list[int]:
    raw = [340, 170]
    for bit_index in range(bit_length):
        raw.append(43)
        raw.append(128 if ((data >> bit_index) & 0x1) else 43)
    raw.append(43)
    return raw


def panasonic_raw_ticks(data: int, bit_length: int) -> list[int]:
    raw = [350, 175]
    for bit_index in range(bit_length):
        raw.append(43)
        raw.append(130 if ((data >> bit_index) & 0x1) else 43)
    raw.append(43)
    return raw


def jvc24_raw_ticks(data: int) -> list[int]:
    raw = [844, 422]
    for bit_index in range(24):
        raw.append(53)
        raw.append(158 if ((data >> bit_index) & 0x1) else 53)
    raw.append(53)
    return raw


def rc5_raw_ticks(data: int) -> list[int]:
    ticks: list[int] = []
    level = True
    current_ticks = 0
    for bit_index in range(13, -1, -1):
        bit = (data >> bit_index) & 0x1
        halves = [True, False] if bit else [False, True]
        for half in halves:
            if half == level:
                current_ticks += 89
            else:
                ticks.append(current_ticks)
                current_ticks = 89
                level = half
    ticks.append(current_ticks)
    return ticks


def rc_biphase_ticks(bits: list[tuple[int, int]], unit_ticks: int, prefix: list[int] | None = None) -> list[int]:
    raw = list(prefix or [])
    level = True
    current_ticks = 0
    for bit, width_halves in bits:
        half_count = width_halves // 2
        halves = ([True] * half_count + [False] * half_count) if bit else ([False] * half_count + [True] * half_count)
        for half in halves:
            if half == level:
                current_ticks += unit_ticks
            else:
                if current_ticks:
                    raw.append(current_ticks)
                current_ticks = unit_ticks
                level = half
    raw.append(current_ticks)
    return raw


def rc6_m0_bits(payload: int, toggle: int = 1) -> int:
    return (1 << 20) | ((toggle & 0x1) << 16) | payload


def rc6_m0_raw_ticks(payload: int, toggle: int = 1) -> list[int]:
    bits: list[tuple[int, int]] = [(1, 4), (0, 2), (0, 2), (0, 2), (toggle, 4)]
    bits.extend(((payload >> bit_index) & 0x1, 2) for bit_index in range(15, -1, -1))
    return rc_biphase_ticks(bits, unit_ticks=44, prefix=[266, 89])


@pytest.mark.parametrize(
    "path",
    sorted(FIXTURE_DIR.glob("*.yaml")),
    ids=lambda p: p.name,
)
def test_verified_fixture_schema(path):
    with path.open(encoding="utf-8") as fh:
        data = yaml.safe_load(fh)

    assert data["name"] == path.stem
    assert data["source"] == "reviewed"
    assert data["tick_us"] == 10
    assert isinstance(data["raw_ticks"], list)
    assert data["raw_ticks"]
    assert all(isinstance(tick, int) for tick in data["raw_ticks"])
    assert all(0 < tick <= 0xFFFF for tick in data["raw_ticks"])


def test_nec_normal_fixture_matches_reviewed_fields():
    data = load_fixture("nec_normal_00ff_34.yaml")
    address = data["fields"]["address"]
    command = data["fields"]["command"]

    assert data["protocol"] == "NEC"
    assert data["frame_type"] == "NORMAL"
    assert data["bit_length"] == 32
    assert data["bits"] == nec_bits(address, command)
    assert data["raw_ticks"] == nec_raw_ticks(address, command)


def test_nec_repeat_fixture_matches_reviewed_timing():
    data = load_fixture("nec_repeat.yaml")

    assert data["protocol"] == "NEC"
    assert data["frame_type"] == "REPEAT"
    assert data["bit_length"] == 0
    assert data["bits"] == 0xFFFFFFFFFFFFFFFF
    assert data["raw_ticks"] == [900, 225, 56]


def test_sony12_fixture_matches_reviewed_timing():
    data = load_fixture("sony12_0a90.yaml")
    frame_data = data["fields"]["data"]

    assert data["protocol"] == "SONY12"
    assert data["frame_type"] == "NORMAL"
    assert data["bit_length"] == 12
    assert data["bits"] == frame_data
    assert data["raw_ticks"] == sony12_raw_ticks(frame_data)


def test_samsung32_fixture_matches_reviewed_fields():
    data = load_fixture("samsung32_e0e0_40bf.yaml")
    address = data["fields"]["address"]
    command = data["fields"]["command"]

    assert data["protocol"] == "SAMSUNG32"
    assert data["frame_type"] == "NORMAL"
    assert data["bit_length"] == 32
    assert data["bits"] == samsung32_bits(address, command)
    assert data["raw_ticks"] == samsung32_raw_ticks(address, command)


def test_aeha48_fixture_matches_reviewed_fields():
    data = load_fixture("aeha48_123456789abc.yaml")
    frame_data = data["fields"]["data"]

    assert data["protocol"] == "AEHA"
    assert data["frame_type"] == "NORMAL"
    assert data["bit_length"] == 48
    assert data["bits"] == frame_data
    assert data["raw_ticks"] == aeha_raw_ticks(frame_data, data["bit_length"])


def test_panasonic48_fixture_matches_reviewed_fields():
    data = load_fixture("panasonic48_40040100bcbd.yaml")
    frame_data = data["fields"]["data"]

    assert data["protocol"] == "PANASONIC48"
    assert data["frame_type"] == "NORMAL"
    assert data["bit_length"] == 48
    assert data["bits"] == frame_data
    assert data["raw_ticks"] == panasonic_raw_ticks(frame_data, data["bit_length"])


def test_jvc24_fixture_matches_reviewed_fields():
    data = load_fixture("jvc24_00c0de.yaml")
    frame_data = data["fields"]["data"]

    assert data["protocol"] == "JVC24"
    assert data["frame_type"] == "NORMAL"
    assert data["bit_length"] == 24
    assert data["bits"] == frame_data
    assert data["raw_ticks"] == jvc24_raw_ticks(frame_data)


@pytest.mark.parametrize("name", ["rc5_3fff.yaml", "rc5_300f.yaml"])
def test_rc5_fixture_matches_reviewed_fields(name):
    data = load_fixture(name)
    frame_data = data["fields"]["data"]

    assert data["protocol"] == "RC5"
    assert data["frame_type"] == "NORMAL"
    assert data["bit_length"] == 14
    assert data["bits"] == frame_data
    assert data["raw_ticks"] == rc5_raw_ticks(frame_data)


def test_rc6_m0_fixture_matches_reviewed_fields():
    data = load_fixture("rc6_m0_11234.yaml")
    payload = data["fields"]["payload"]

    assert data["protocol"] == "RC6_M0_16"
    assert data["frame_type"] == "NORMAL"
    assert data["bit_length"] == 21
    assert data["bits"] == rc6_m0_bits(payload, toggle=1)
    assert data["raw_ticks"] == rc6_m0_raw_ticks(payload, toggle=1)


def test_cpp_fixture_header_is_current():
    generated = subprocess.check_output([sys.executable, str(CPP_EXPORTER)], text=True)
    assert CPP_HEADER.read_text(encoding="utf-8") == generated
