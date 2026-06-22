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
uv run --env-file .env pytest -s -o python_files="study_*.py" studies/carrier_loopback/
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
uv run --with matplotlib python studies/carrier_loopback/analyze.py \
  studies/carrier_loopback/data/carrier_loopback.txt --plot out.png
```

Key fields:

- `carrier period: mean .. within-mark sd ..` — period cleanliness. A large sd
  (> ~2 µs) implicates **TX resolution** dithering.
- `cycles/mark` and `carrier-on span sd` — mark-to-mark spread. A non-zero
  spread implicates a **free-running carrier** (phase drift).

The verdict guides the root-cause fix: a finer TX resolution helps only the
period-quantization mechanism; a per-mark carrier phase reset is needed for the
free-running-phase mechanism.

## Observed results (2026-06-18)

Run: 80 marks, commanded `CAP 530 530 4 33 38000` (the library rounds JVC's
525 µs mark to 53 ticks = **530 µs**, so this is exactly what JVC emits).

```
carrier period : mean 26.23 us, within-mark sd 0.67 us
cycles/mark    : [20, 21]   (43x 20-cycle, 37x 21-cycle  -> ~54% / 46%)
carrier-on span: mean 517.9 us, sd 10.28 us, range 508..530 us  (~±1 carrier period)
```

Verdict:

- **Carrier period is clean** (26.23 µs ≈ 38.1 kHz, within-mark sd 0.67 µs), and
  the high/low widths (~8.7 / ~17.6 µs) are **not** multiples of the 10 µs symbol
  tick — so the carrier is generated from a finer clock than the RMT resolution.
  **Period quantization / TX resolution is NOT the cause; raising TX resolution
  would not change the period.**
- **The cycle count per identical mark is a near coin-flip between 20 and 21**
  (530 µs / 26.23 µs = 20.21), with the carrier-on span spread ~±1 carrier period.
  This is the signature of a **free-running carrier**: the mark boundary lands at
  a random phase, capturing 20 or 21 whole cycles. **Confirmed root cause.**

Which way it falls for JVC: ~50/50 per mark. At the TSOP, a 21-cycle mark reads
longer → following space shorter (~538 µs, inside IRremoteESP8266's ~594 µs JVC
window = OK); a 20-cycle mark reads shorter → space longer (~620 µs, over the
window = NG). A JVC decode needs *every* zero-space under 594 µs, so with ~half
the bits on the NG side almost every 16-bit frame has at least one over-window
bit → mostly fails, occasionally (all bits land OK) passes — matching the ~1/5
seen in `compat_matrix/irremoteesp8266_rx[JVC]`.

Implication for the fix: align the carrier phase per mark so each mark holds a
deterministic integer number of cycles. The hardware `rmt_apply_carrier` runs
continuously (no per-mark phase reset), so the likely route is to **encode the
carrier explicitly as RMT symbols** — which needs ~1 µs resolution to represent
the ~8.7 / 17.6 µs half-cycles. So finer resolution matters as an *enabler of the
fix*, not because the period is currently wrong.

### Mark-width sweep (`CL_MARKS="510,520,530,540,550"`, 120 marks each)

| mark µs | mark/period | cycle split | minority % | on-span sd |
|---|---|---|---|---|
| 510 | 19.45 | 19:34 / 20:86 | 28 % | 10.4 µs |
| **520** | 19.84 | 20:106 / 21:14 | **12 %** | **4.8 µs** |
| **530** (JVC) | 20.21 | 20:63 / 21:57 | **48 %** | **10.3 µs** |
| **540** | 20.60 | 20:15 / 21:105 | **12 %** | 8.1 µs |
| 550 | 20.98 | 21:85 / 22:35 | 29 % | 7.2 µs |

The cycle distribution shifts monotonically with mark width (on-span mean tracks
the +10 µs steps exactly: 498→508→518→528→539 µs), and the **jitter oscillates**:
a near-50/50 coin-flip at ~530 µs (half-integer cycles, worst) and tight ~12 %
minorities at ~520 / ~540 µs (near integer cycles). **The library emits 530 µs
for JVC's 525 µs mark (10 µs tick), landing on the worst point** — its on-span sd
(10.3 µs) is ~2× that of 520 µs (4.8 µs).

Note on direction: at the receiver a *lower* carrier-cycle count makes the
demodulated mark shorter and the following space ~1 period (≈26 µs) *longer* —
that is the >594 µs failing case. So the widths that settle on the lower count
(510/520 → mostly 20 cycles) bias toward the *bad* side even though their
*jitter magnitude* (on-span sd) is small; the widths that settle on the higher
count (540 → mostly 21) bias toward the *safe* (shorter-space) side. This is why
picking a low-jitter width is not enough — it must be the low-jitter width that
also lands on the higher cycle count.

Important caveat: the TX cycle distribution does **not** map linearly to the
received space — at 530 µs the TX is ~50/50 but only ~18 % of received
zero-spaces exceeded 594 µs (the TSOP demod is non-linear). So whether 520 or 540
actually keeps the received space under IRremoteESP8266's ~594 µs JVC window must
be **measured through the real receiver**, not inferred from this loopback alone.
Full determinism still needs per-mark carrier phase alignment, and an exact
integer-cycle width is unreachable at the 10 µs tick — both point to a
finer-resolution, symbol-encoded carrier.
