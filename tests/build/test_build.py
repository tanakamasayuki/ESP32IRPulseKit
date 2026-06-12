import shutil
import subprocess
from pathlib import Path

import pytest


def compile_sketch(sketch: Path, tmp_path: Path):
    if shutil.which("arduino-cli") is None:
        pytest.skip("arduino-cli is not installed")

    build_path = tmp_path / sketch.name
    cmd = [
        "arduino-cli",
        "compile",
        "--profile",
        "esp32",
        "--build-path",
        str(build_path),
        str(sketch),
    ]
    subprocess.run(cmd, check=True, cwd=sketch.parents[2])


def test_codec_smoke_builds(tmp_path):
    compile_sketch(Path(__file__).with_name("codec_smoke"), tmp_path)


def test_rx_dump_example_builds(tmp_path):
    repo = Path(__file__).resolve().parents[2]
    compile_sketch(repo / "examples" / "01_rx_dump", tmp_path)
