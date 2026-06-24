# Studies

> Japanese: [README.ja.md](README.ja.md)

On-demand board investigations that record observation logs rather than a pass/fail. They characterize physical behavior (carrier-phase jitter, timing sweeps, link quality) and compare against external libraries. Conclusions need a human to read the logs and plots.

These are **not** auto-collected. The pytest files are named `study_*.py`, not `test_*.py`, so `pytest .` and `pytest hardware` skip them entirely. Run one on demand with an explicit pattern override:

```sh
uv run --env-file .env pytest -s -o python_files="study_*.py" studies/carrier_jitter/
```

`analyze.py` / `monitor.py` are plain scripts, run directly:

```sh
uv run python studies/jvc_timing_sweep/analyze.py studies/jvc_timing_sweep/data/jvc_sweep.csv
```

| Directory | Investigates |
| --- | --- |
| `carrier_jitter/` | Demodulated mark-edge stability vs mark width, carrier frequency, and duty |
| `carrier_loopback/` | Carrier-phase behavior captured at 1 µs without a TSOP |
| `jvc_timing_sweep/` | JVC zero-space window vs external decoders |
| `jvc_verify_arduino/` | JVC decode against Arduino-IRremote |
| `tx_jitter/` | TX envelope jitter across PulseKit and external libraries (over the air) |
| `tx_jitter_loopback/` | Same, on a wired single-board loopback rig |
| `link_quality/` | Live link-quality meter |
| `compat_matrix/` | Protocol differences, bit order, and external-library compatibility |
| `compat_matrix_ac/` | Air-conditioner state-field compatibility vs external AC libraries (calibrates the Panasonic field map) |

Reproducible findings here should be promoted into `pc/fixtures` and `hardware/`.
