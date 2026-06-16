# Tests

> Japanese: [README.ja.md](README.ja.md)

ESP32IRPulseKit tests are grouped by whether the test inputs and environment can be fully controlled in software. See [TEST_PLAN.md](TEST_PLAN.md) for the overall strategy and coverage table.

## Requirements

- `uv`
- Host tests: Arduino CLI and `lang-ship:host`
- Build tests: Arduino CLI and ESP32 Arduino Core
- Hardware tests: Arduino CLI, ESP32 Arduino Core, target ESP32 boards

## Run

Run from the `tests/` directory.

Do not run bare `pytest`. The `hardware/` tree contains tests that require physical boards and local serial ports, so always select a parent directory explicitly.
First copy `.env.example` to `.env`, then edit `.env` for your local serial ports and GPIOs.

```sh
cp .env.example .env
uv run --env-file .env pytest host
uv run --env-file .env pytest build
uv run pytest fixtures
uv run --env-file .env pytest hardware/link_smoke
```

## Layout

- `host/`: Arduino host runtime tests for codec, protocol specs, and frame conversion.
- `build/`: Arduino CLI build tests for examples and minimal sketches.
- `hardware/`: ESP32 hardware automated tests for RMT paths. `hardware/link_smoke` is the stable two-board release-gate smoke test.
- `manual/`: tests that require real remotes, distance/angle checks, ambient light checks, or human observation.
- `fixtures/`: shared IR signal data for host, hardware, and manual tests.
