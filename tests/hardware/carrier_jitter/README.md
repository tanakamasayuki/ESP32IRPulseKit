# Carrier / mark-width jitter probe

> Japanese: [README.ja.md](README.ja.md)

Directly studies how the **IR carrier (frequency, duty) and the mark width**
affect the post-TSOP demodulated edge jitter. Both transmit and receive use
**raw RMT at 1 us resolution**.

- **TX (peer_tx/)**: `rmtInit(.., RMT_TX_MODE, .., 1 MHz)` + `rmtSetCarrier()`
  transmits a NEC-shaped frame (9000/4500 header + 32 bit-marks + stop mark). The
  payload is arbitrary (only the marks are studied). The **bit-mark width, the
  carrier frequency, and the duty** are swept.
- **RX (carrier_jitter.ino)**: same 1 us RMT capture as `tx_jitter/`, dumping each
  edge width as `RX_JITTER`.
- Path is **IR LED -> air -> TSOP** (same two-board wiring as `tx_jitter/`).
  Because the point is to observe carrier demodulation, it must go through the
  TSOP, not a wired loopback.

## Why

A 38 kHz period is `1e6/38000 = 26.316 us`. A 560 us NEC mark is
`560 / 26.316 = 21.28` periods, so it ends mid-cycle and the TSOP's decision of
whether the last pulse counts wanders frame-to-frame, moving the demodulated edge
by up to one carrier period. A width near an integer number of periods
(`21 * 26.316 = 552.6 us`, ~553 us) ends on a cycle boundary and should be more
stable. This rig measures jitter-vs-mark-width and quantifies the **difference
between 553 us and 560 us**.

## Wiring

Same as `tx_jitter/`: TX board `TEST_IR_TX_GPIO` (IR LED) -> RX board
`TEST_IR_RX_GPIO` (TSOP). Reuses the existing `.env` keys.

## Run

```sh
cd tests
# Rough trend (defaults: 38 kHz, duty 20/33/50, mark 540..580, 20 frames each)
uv run --env-file .env pytest -s hardware/carrier_jitter/

# Fine (e.g. 549..556 in 1 us steps, duty 33 only, 60 frames for a clean graph)
CARRIER_MARKS="549,550,551,552,553,554,555,556" CARRIER_DUTY="33" CARRIER_FRAMES="60" \
  uv run --env-file .env pytest -s hardware/carrier_jitter/
```

Sweeps are env-overridable: `CARRIER_HZ` (default `38000`), `CARRIER_DUTY`
(default `20,33,50`, percent), `CARRIER_MARKS` (default
`540,545,550,553,555,560,565,570,575,580`, us), `CARRIER_FRAMES` (default `20`),
`CARRIER_CSV` (default `/tmp/carrier_jitter.csv`).

## Output

One line per measurement point (visible with `-s`):

```text
CARRIER_PROBE hz=38000 duty=33 mark=560 cycles=21.280 frames=20 \
  mark_sd=.. mark_max_sd=.. mark_max_ptp=.. overall_mean_sd=..
```

- `cycles` — `mark / (1e6/hz)`; closer to an integer should be more stable.
- `mark_sd` — mean stdev of the **width-W mark edges** (even index, mean near the
  commanded width). The headline metric.
- `mark_max_sd` / `mark_max_ptp` — worst mark edge.
- `overall_mean_sd` — mean over all edges (reference).

A CSV is written to `CARRIER_CSV` at the end (columns:
`carrier_hz,duty_pct,mark_us,cycles,frames,mark_sd_us,mark_max_sd_us,mark_max_ptp_us,overall_mean_sd_us`),
ready to plot as jitter-vs-mark-width with one series per duty.

## Workflow

1. Run the default coarse sweep to find where the jitter dip sits.
2. Re-sweep around the dip (~552-553 us) in 1 us steps with more frames; quantify
   the optimum and its delta vs 560 us from the CSV.
3. Optionally sweep carrier frequency too, to confirm the optimum mark tracks
   `integer * period`.

## Observed results (38 / 36 / 40 kHz, TSOP is a 38 kHz part)

Representative run (NEC-shaped, over-the-air, two ESP32-S3). Absolute numbers
depend on the setup; the relative picture is the point. Graphs are in `data/`
(regenerate with `analyze.py`).

![jitter vs mark width @38kHz](data/jitter_vs_mark_38000.png)
![jitter vs duty @38kHz](data/jitter_vs_duty_38000.png)
![carrier compare](data/compare_carriers.png)

- **Jitter oscillates with mark width** (graph 1). Valleys sit near integer-cycle
  widths (38 kHz: ~550 = 21c / ~581 = 22c); peaks near half-integer (~570 ~= 21.5c,
  where the mark ends mid-cycle and the demodulation point is most ambiguous).
- **553 us vs 560 us**: 553 < 560 at every duty (553 ~= 21.0 cycles = valley,
  560 ~= 21.28 = toward a peak). The gap is largest at low duty (15%: 553 ~= 9.8 vs
  560 ~= 13.5 us). The true minimum is a bit lower still (~549-550 us).
- **Duty dependence** (graph 2): in this rig lower duty gave lower jitter
  (15% ~= 4.5 us up to 50% ~= 31 us).
- **The effect is maximal at the TSOP's tuned frequency** (compare): 38 kHz has
  the sharpest oscillation and lowest valley; 36 kHz raises the baseline
  everywhere and 40 kHz flattens it (off-center can't exploit the integer-cycle
  valleys). The response is asymmetric.

### Practical guidance and a caveat

- For a 38 kHz TSOP, the most stable demodulated edges come from **(1) a mark
  width at an integer number of 38 kHz cycles (~550 / ~581 us), and (2) a carrier
  at the TSOP's tuned frequency.** The 560 us NEC standard sits near a local peak.
- **The duty conclusion is environment-dependent - be careful**: this rig placed
  TX and RX **under 10 cm apart**, so the TSOP was likely **IR-saturated**, which
  can make "lower duty is better" appear. At greater distance / off-axis angles /
  ambient light, **higher duty usually wins** on range and S/N, and the ranking
  may invert. Read the duty trend here as a close-range, no-load result only.
- The geometric "valley at integer cycles" effect should persist with distance,
  but the exact optimum width (offset a few us from the integer point by the
  TSOP's integration delay) and the amplitude depend on received strength, i.e.
  distance and angle.
