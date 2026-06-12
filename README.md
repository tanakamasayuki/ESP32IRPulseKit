# ESP32IRPulseKit

> Japanese: [README.ja.md](README.ja.md)

ESP32 Arduino Core 3.x IR remote send/receive library built on the ESP-IDF 5.x RMT driver.

This repository is pre-release. The external specification is in [SPEC.ja.md](SPEC.ja.md), and the test strategy is in [tests/TEST_PLAN.ja.md](tests/TEST_PLAN.ja.md).

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
uv run --env-file .env pytest host
uv run --env-file .env pytest build
uv run pytest fixtures
```

Do not run bare `pytest`; select `host`, `build`, `fixtures`, or `hardware/tx_rx` explicitly. `host/` is for Arduino host runtime tests, `build/` is for Arduino CLI build tests, and `hardware/` is for ESP32 hardware tests. See [tests/README.md](tests/README.md) for details.
