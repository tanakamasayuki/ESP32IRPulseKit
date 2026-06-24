import shutil
import subprocess
from pathlib import Path

import pytest


def compile_sketch(sketch: Path, tmp_path: Path, profile: str = "esp32"):
    if shutil.which("arduino-cli") is None:
        pytest.skip("arduino-cli is not installed")

    repo = Path(__file__).resolve().parents[3]
    build_path = tmp_path / sketch.name
    cmd = [
        "arduino-cli",
        "compile",
        "--profile",
        profile,
        "--build-path",
        str(build_path),
        str(sketch),
    ]
    subprocess.run(cmd, check=True, cwd=repo)


def test_codec_smoke_builds(tmp_path):
    compile_sketch(Path(__file__).with_name("codec_smoke"), tmp_path)


def test_rx_dump_example_builds(tmp_path):
    repo = Path(__file__).resolve().parents[3]
    compile_sketch(repo / "examples" / "01_rx_dump", tmp_path)


def test_nec_tx_example_builds(tmp_path):
    repo = Path(__file__).resolve().parents[3]
    compile_sketch(repo / "examples" / "02_nec_tx", tmp_path)


def test_send_protocols_example_builds(tmp_path):
    repo = Path(__file__).resolve().parents[3]
    compile_sketch(repo / "examples" / "03_send_protocols", tmp_path)


def test_learn_example_builds(tmp_path):
    repo = Path(__file__).resolve().parents[3]
    compile_sketch(repo / "examples" / "04_learn", tmp_path)


def test_raw_monitor_example_builds(tmp_path):
    repo = Path(__file__).resolve().parents[3]
    compile_sketch(repo / "examples" / "05_raw_monitor", tmp_path)


def test_link_smoke_hardware_sketches_build(tmp_path):
    repo = Path(__file__).resolve().parents[3]
    compile_sketch(repo / "tests" / "hardware" / "link_smoke", tmp_path, "rx_esp32s3")
    compile_sketch(repo / "tests" / "hardware" / "link_smoke" / "peer_tx", tmp_path, "tx_esp32s3")


def test_protocol_matrix_hardware_sketches_build(tmp_path):
    repo = Path(__file__).resolve().parents[3]
    matrix = repo / "tests" / "hardware" / "protocol_matrix"
    compile_sketch(matrix, tmp_path, "rx_esp32s3")
    compile_sketch(matrix / "peer_tx", tmp_path, "tx_esp32s3")
