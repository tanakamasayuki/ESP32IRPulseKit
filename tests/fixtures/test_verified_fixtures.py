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


def test_cpp_fixture_header_is_current():
    generated = subprocess.check_output([sys.executable, str(CPP_EXPORTER)], text=True)
    assert CPP_HEADER.read_text(encoding="utf-8") == generated
