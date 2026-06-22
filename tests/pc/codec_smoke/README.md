# Codec Smoke (Arduino host)

> Japanese: [README.ja.md](README.ja.md)

Runtime logic tests for an Arduino host environment live here.

The goal is to run assertions for logic that does not require real RMT hardware while keeping the test environment close to Arduino users.

Scope:

- `Frame::toBits()` / `fromBits()`
- `encodeBitsToRaw()` / `decodeRawToBits()`
- Protocol spec values
- Tolerance boundaries
- Score and candidate ordering
- Repeat decode
- Decoding fixture RAW into expected BITS

RMT TX/RX, GPIO inversion, idle threshold, and real IR transmission are covered by `tests/hardware/`.

The host profile uses `lang-ship:host`. `sketch.yaml` has a `host` profile and, when useful, an `esp32` profile for hardware execution through pytest-embedded + Arduino CLI.

```sh
uv run --env-file .env pytest pc/codec_smoke
uv run --env-file .env pytest pc/codec_smoke --profile=esp32
```

`verified_fixtures.h` is generated from `../fixtures/` by `../fixtures/export_cpp_fixtures.py`; do not edit it by hand.
