# JVC timing sweep (mark × zero-space)

> Japanese: [README.ja.md](README.ja.md)

Finds which emitted JVC timing best survives **IRremoteESP8266**'s decoder, by
sweeping the **bit-mark** and **zero-space** over a 2D grid and measuring, per
cell, the received zero-space margin and the decode pass ratio. This is the
"cheap mitigation" search that complements the root-cause work in
[../carrier_loopback/](../carrier_loopback/) (free-running carrier phase).

- **TX (peer_tx/)**: ESP32IRPulseKit. Command
  `JVCRAW <mark_us> <zero_space_us> <one_space_us> <hex_bits>` builds a JVC-shaped
  frame (fixed 8400/4200 header, 16 bits LSB-first, trailer mark) with the given
  timing and the library default carrier (38 kHz / 0.33).
- **RX (jvc_timing_sweep.ino)**: IRremoteESP8266. Dumps the raw capture (µs) on
  **every** frame, so the host reads the received zero-space even when it decodes.

Path is IR LED → air → TSOP (two boards), same wiring as `compat_matrix/`.

## Why 2D

Received zero-space ≈ `space_env + (mark_env − demodulated_mark)`. The
**zero-space** shifts the received space ~linearly; the **mark** moves the
demodulated length in ~1-carrier-period steps (cycle counting). IRremoteESP8266's
zero-space ceiling is **fixed at `(526−50)×1.25 ≈ 595 µs`** regardless of what we
send, so lowering the received zero-space buys margin. The 2D grid reveals the
(usually striped) response surface and the most robust operating point.

## Run

```sh
cd tests
uv run --env-file .env pytest -s -o python_files="study_*.py" studies/jvc_timing_sweep/
```

Env overrides: `JS_MARKS` (`500,510,…,560`), `JS_ZSPACES` (`470,490,…,550`),
`JS_ONE` (`1575`, held at the JVC standard), `JS_N` (`12` frames/cell),
`JS_BITS` (`c0de`), `JS_CSV` (`data/jvc_sweep.csv`).

The test prints a per-cell line and the best cell, and writes a CSV.

## Analyze

```sh
uv run --with matplotlib python studies/jvc_timing_sweep/analyze.py \
  studies/jvc_timing_sweep/data/jvc_sweep.csv
```

Two heatmaps (rows = zero-space, cols = mark): `margin_p90` (595 − p90 received
zero-space; higher = more headroom = the optimum) and `pass_ratio`.

## Observed results (2026-06-22)

7 marks × 5 zero-spaces, N=12, one-space 1575, bits 0xC0DE, through IRremoteESP8266.

```
pass_ratio   rows = zero-space us, cols = bit-mark us
zsp\mark  500   510   520   530   540   550   560
  470    0.42  0.67  0.75  1.00  1.00  1.00  1.00
  490    0.58  0.83  0.50  1.00  1.00  1.00  1.00
  510    0.33  0.75  0.75  0.92  0.92  1.00  1.00
  530    0.00  0.25  0.17  0.33  0.33  0.42  0.42   <- JVC default (530/530)
  550    0.00  0.00  0.00  0.00  0.00  0.00  0.00
```

Both axes matter:

- **Zero-space (dominant).** Lowering it drops the received zero-space ~linearly:
  margin-to-595 is +75 / +49 / +23 / −3 / −6 µs at zspace 470 / 490 / 510 / 530 /
  550. At 550 it fails everywhere.
- **Mark (secondary, real).** Within a row, a larger mark raises the pass ratio —
  e.g. at zspace 530: mark 500 → 0.00, mark 560 → 0.42, and `zero_max` drops from
  ~622 µs to ~602 µs. This confirms the carrier-cycle effect from
  `carrier_loopback/`: a longer mark lands on more carrier cycles (21 not 20), so
  the demodulated mark is longer and the worst-case space shorter.
  (Note: the `margin_p90` heatmap looks flat across mark because decode pass/fail
  is set by the *worst* zero bit, not the p90 — the mark effect shows in
  `pass_ratio` / `zero_max`, not p90.)

**Most robust region: mark ≥ 530 µs AND zero-space ≤ 490 µs → pass 1.00.** The JVC
default (530/530, i.e. the library's rounding of 525) sits at 0.33. A candidate
mitigation is e.g. mark ≈ 540 / zero-space ≈ 480 (within JVC tolerance), but see
the caveats — this is tuned to one receiver and must be cross-checked.

## Caveats

- **Per-receiver tuning / overfitting.** A timing that pleases IRremoteESP8266
  may hurt Arduino-IRremote or real remotes. Re-check the winning cell against
  the other receivers (`compat_matrix/`) and our own RMT RX before adopting.
- **Carrier-phase floor.** The free-running carrier leaves a residual ±1-cycle
  jitter, so no (mark, zspace) reaches perfect determinism — see
  `carrier_loopback/`. This sweep finds the best *mitigation*, not a full fix.
- Keep timings inside each decoder's tolerance window, and remember deviating
  from the JVC standard (525/525) is an environment-specific choice to document.
