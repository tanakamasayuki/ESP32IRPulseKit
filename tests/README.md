# Tests

> Japanese: [README.ja.md](README.ja.md)

ESP32IRPulseKit tests are grouped by whether the test inputs and environment can be fully controlled in software. See [TEST_PLAN.md](TEST_PLAN.md) for the overall strategy and coverage table.

## Requirements

- `uv`
- Host tests: Arduino host runtime environment
- Build tests: Arduino CLI and ESP32 Arduino Core
- Hardware tests: Arduino CLI, ESP32 Arduino Core, target ESP32 boards

## Run

Run from the `tests/` directory.

```sh
uv run pytest host
uv run pytest build
```

## Layout

- `host/`: Arduino host runtime tests for codec, protocol specs, and frame conversion.
- `build/`: Arduino CLI build tests for examples and minimal sketches.
- `hardware/`: ESP32 hardware automated tests for TX/RX RMT paths.
- `manual/`: tests that require real remotes, distance/angle checks, ambient light checks, or human observation.
- `fixtures/`: shared IR signal data for host, hardware, and manual tests.
