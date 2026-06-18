# Carrier loopback probe (single board)

> Japanese: [README.ja.md](README.ja.md)

Looks **directly at the carrier** the library emits, with the TSOP removed from
the path, to settle *why* PulseKit→IRremoteESP8266 JVC is marginal. One board
transmits a short pattern of **carrier-modulated marks** with the library TX and
captures the **wire-looped electrical signal at 1 µs** (RMT, no demodulator), so
every carrier half-cycle is a separate edge.

Wire **`LOOPBACK_TX_GPIO` → `LOOPBACK_RX_GPIO`** directly (default GPIO5 → GPIO6,
from `.env`). No IR LED, no TSOP.

## What it answers

The JVC compat jitter was traced to the transmitter. Two TX-side mechanisms are
possible; this rig separates them:

- **Carrier-period quantization** — at the library's 100 kHz (10 µs) RMT
  resolution, a 38 kHz carrier is 2.63 ticks/period and cannot divide evenly, so
  the period may dither. → shows up as a large *within-mark* period spread.
- **Free-running carrier phase** — the RMT carrier runs continuously, so the
  phase at each mark boundary drifts mark-to-mark. → shows up as the carrier-on
  span / cycle count *varying mark-to-mark* even for identical commanded marks.

## Run

```sh
cd tests
uv run --env-file .env pytest -s hardware/carrier_loopback/
```

Each `CAP <mark_us> <space_us> <count> [duty_pct] [carrier_hz]` sends `count`
carrier marks and dumps the raw 1 µs capture. Env overrides:
`CL_MARK` (530), `CL_SPACE` (530), `CL_COUNT` (4), `CL_DUTY` (33),
`CL_HZ` (38000), `CL_SENDS` (20), `CL_OUT` (`data/carrier_loopback.txt`).

Keep the pattern short: with the carrier ON each mark is ~1 edge-pair per 26 µs,
and the capture is bounded by `RMT_MEM_NUM_BLOCKS_4` (~192 symbols). 4 × 530 µs
marks (~80 cycles) is safe; a full protocol frame would overflow.

## Output / analysis

The test prints a summary and saves the raw dump to `data/`. Re-analyze (or plot)
offline:

```sh
uv run --with matplotlib python hardware/carrier_loopback/analyze.py \
  hardware/carrier_loopback/data/carrier_loopback.txt --plot out.png
```

Key fields:

- `carrier period: mean .. within-mark sd ..` — period cleanliness. A large sd
  (> ~2 µs) implicates **TX resolution** dithering.
- `cycles/mark` and `carrier-on span sd` — mark-to-mark spread. A non-zero
  spread implicates a **free-running carrier** (phase drift).

The verdict guides the root-cause fix: a finer TX resolution helps only the
period-quantization mechanism; a per-mark carrier phase reset is needed for the
free-running-phase mechanism.
