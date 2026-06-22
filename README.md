# ESP32IRPulseKit

> Japanese: [README.ja.md](README.ja.md)

ESP32 Arduino Core 3.x IR remote send/receive library built on the ESP-IDF 5.x RMT driver.

This repository is pre-release. The external specification is in [SPEC.md](SPEC.md), implementation notes are in [DESIGN.md](DESIGN.md), and the test strategy is in [tests/TEST_PLAN.md](tests/TEST_PLAN.md).

## Current Direction

- Keep RAW tick capture, decode candidates, protocol specs, and frame conversion separated.
- Stabilize the codec path with Arduino host runtime tests first.
- Verify examples and minimal sketches with Arduino CLI build tests.
- Validate RMT/HAL behavior on real ESP32 hardware.
- Keep manual tests only for behavior that cannot be controlled fully in software.

## Tests

Tests live under `tests/`.

```sh
cd tests
cp .env.example .env
# Edit .env for your local serial ports and GPIOs.
uv run --env-file .env pytest pc
uv run --env-file .env pytest hardware/link_smoke
```

`pytest pc` runs all PC tests (`fixtures`, `codec_smoke`, `compile`); `hardware/` holds the two-board pass/fail regression tests. Board investigations under `studies/` are not auto-collected (`study_*.py`). See [tests/README.md](tests/README.md) for details.
