# Signal Fixtures

> Japanese: [README.ja.md](README.ja.md)

IR signal data used by tests lives here.

## Types

- `generated/`: ideal waveforms generated from protocol specs and BITS
- `verified/`: hand-written or reviewed fixed RAW data
- `captured/`: raw captures from real remotes or hardware tests before review

RAW data should be stored as `raw_ticks` where `1 tick = 10us`, matching the library API. `raw_us` may be included when it helps review.
