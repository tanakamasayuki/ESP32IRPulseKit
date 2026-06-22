import re


def test_codec_smoke(dut):
    match = dut.expect(re.compile(rb"TEST done (\d+)/(\d+)"), timeout=30)
    assert match.group(1) == match.group(2)
