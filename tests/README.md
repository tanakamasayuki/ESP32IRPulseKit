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

## Pre-release check

Before cutting a release (running the **Release** GitHub Action), run the full suite on the two-board rig with `.env` configured:

```sh
uv run --env-file .env pytest -v                                                       # PC tests + example builds + two-board hardware tests
uv run --env-file .env pytest studies/compat_matrix*/ -o python_files="study_*.py" -v  # cross-implementation compat (generic + A/C)
```

- The first command collects `pc/` (host codec/frame logic, example + sketch compile, fixtures) **and** `hardware/` (`link_smoke`, `protocol_matrix`, `protocol_matrix_ac`), so it needs the two-board rig. It does **not** collect `studies/` (those are `study_*.py`, not `test_*.py`). `protocol_matrix` covers the generic protocols (NEC/Sony/Samsung/JVC/AEHA/RC5/RC6); `protocol_matrix_ac` covers the A/C layer as a PulseKit self round-trip (TX -> RX, one state per vendor) — both are gated pass/fail.
- The second command runs the compat-matrix studies against the external libraries (IRremoteESP8266, HeatpumpIR, Arduino-IRremote) — the cross-implementation interop that backs each A/C vendor's "Supported" status. `compat_matrix*` matches both `compat_matrix/` (generic protocols) and `compat_matrix_ac/` (A/C vendors).

Those two runs cover everything checked for a release: host logic, that every example/sketch compiles, gated two-board round-trip for both generic protocols and every A/C vendor, and cross-implementation compatibility (via the studies). Notes:

- The self-tests (`*_self`) are **not** gated on decode success — they pass as long as the boards boot and the send/receive loop runs (both sides are the same library, so they only serve as an environment baseline).
- No rig connected? Run the host-only subset — `uv run --env-file .env pytest pc -v` (codec_smoke + example builds + fixtures). The unqualified `pytest -v` and the studies command both need boards.

## Layout

- `pc/`: automated tests that run on a PC with no board (CI runs the whole folder).
  - `pc/fixtures/`: shared IR signal data plus Python checks on that data.
  - `pc/codec_smoke/`: codec / protocol / frame logic run on an Arduino host build.
  - `pc/compile/`: ESP32 build-only checks for examples and minimal sketches.
- `hardware/`: ESP32 two-board automated tests that produce pass/fail. `hardware/link_smoke` is the stable release-gate smoke test; `hardware/protocol_matrix` (generic protocols) and `hardware/protocol_matrix_ac` (the `ac::` layer) are the PulseKit self round-trip matrices.
- `studies/`: on-demand board investigations that record observation logs (jitter, timing sweeps, external-library compatibility). Not auto-collected; conclusions need a human.
