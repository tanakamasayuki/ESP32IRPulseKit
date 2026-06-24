# Tests

> Japanese: [README.ja.md](README.ja.md)

ESP32IRPulseKit tests are grouped by whether the test inputs and environment can be fully controlled in software. See [TEST_PLAN.md](TEST_PLAN.md) for the overall strategy and coverage table.

## Requirements

- `uv`
- `pc/codec_smoke`: Arduino CLI and `lang-ship:host`
- `pc/compile`: Arduino CLI and ESP32 Arduino Core
- `hardware/`: Arduino CLI, ESP32 Arduino Core, target ESP32 boards

## Run

Run from the `tests/` directory. Tests are grouped so you can run a whole top-level folder at once.
First copy `.env.example` to `.env`, then edit `.env` for your local serial ports and GPIOs.

```sh
cp .env.example .env
uv run --env-file .env pytest pc                 # all PC tests (CI runs this)
uv run pytest pc/fixtures                         # or one subdirectory
uv run --env-file .env pytest hardware/link_smoke # board pass/fail regression
```

`pytest .` and `pytest hardware` deliberately do NOT collect anything under `studies/`: those files are named `study_*.py`, not `test_*.py`. Run a study on demand with an explicit pattern override:

```sh
uv run --env-file .env pytest -s -o python_files="study_*.py" studies/carrier_jitter/
```

## Layout

- `pc/`: automated tests that run on a PC with no board (CI runs the whole folder).
  - `pc/fixtures/`: shared IR signal data plus Python checks on that data.
  - `pc/codec_smoke/`: codec / protocol / frame logic run on an Arduino host build.
  - `pc/compile/`: ESP32 build-only checks for examples and minimal sketches.
- `hardware/`: ESP32 two-board automated tests that produce pass/fail. `hardware/link_smoke` is the stable release-gate smoke test.
- `studies/`: on-demand board investigations that record observation logs (jitter, timing sweeps, external-library compatibility). Not auto-collected; conclusions need a human.
