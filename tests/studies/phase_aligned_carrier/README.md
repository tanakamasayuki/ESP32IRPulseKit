# Phase-aligned carrier (experiment)

> Japanese: [README.ja.md](README.ja.md)

A/B experiment for the TX carrier-phase limitation in DESIGN §12.

The library normally drives the carrier with the free-running hardware modulator
(`rmt_apply_carrier`), whose phase is not reset per mark, so a mark ends at a
random carrier phase and the demodulated mark width wobbles by up to ±1 carrier
cycle (~26 µs at 38 kHz). This study measures whether the **experimental
phase-aligned path** (`IRSender::setPhaseAlignedCarrier(true)`) removes that
wobble. That path runs the RMT channel at 1 µs and encodes the carrier as
phase-aligned symbols (an integer number of full cycles per mark), so every mark
starts at phase 0.

- `peer_tx/` (TX): sends a NEC-shaped RAW frame through the library `IRSender`,
  switchable per command between `hw` (hardware carrier) and `pa` (phase-aligned).
- `phase_aligned_carrier.ino` (RX dut): captures at 1 µs and prints `RX_JITTER`.
- `study_phase_aligned_carrier.py`: sends many frames in each mode and compares
  the per-mark standard deviation / peak-to-peak across frames.

Both sketches are spec-correct uses of the library; the experiment only changes
the carrier generation method.

## Run

Not auto-collected (`study_*.py`). Run on demand on the two-board rig:

```sh
uv run --env-file .env pytest -s -o python_files="study_*.py" studies/phase_aligned_carrier/
```

Read the `PHASE_ALIGNED_COMPARE mark=… hw_sd=… pa_sd=…` lines: if `pa_sd` and
`pa_ptp` are markedly lower than `hw_*`, the phase-aligned path fixes the wobble
and is a candidate to become the default carrier path (and to retire the JVC
timing tweak / fix AEHA→IRremoteESP8266). Tune the sweep with `PA_MARKS`,
`PA_CARRIER_HZ`, `PA_DUTY`, `PA_FRAMES`.
