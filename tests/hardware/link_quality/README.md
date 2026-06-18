# Link-quality meter (manual)

> Japanese: [README.ja.md](README.ja.md)

A hand-driven IR **environment meter**, not a pass/fail test. The TX board sends
a fixed NEC frame in a loop; the RX board demodulates it; the host prints a
single, live-updating console line scoring the link. Move the two boards around
(distance, angle, obstacles) and watch the score and verdict change in real
time. Stop with **Ctrl-C**; a summary is printed.

It exists because the compat-matrix NEC failures turned out to be a
**receiver-side demodulation artifact that depends on geometry**: at very short
range the TSOP saturates and reports marks *short* / spaces *inflated* (the
opposite of a normal-distance TSOP), which pushes the zero-space past the
decode tolerance — especially the tighter window of external receivers. This
meter lets you find a good physical placement by hand.

## What it shows

```
[GOOD                       ] decode 100%(30/30)  recv 100%  mark 561(+1)sd5  sp0 590(+30)sd11  sp1 1702  compat-margin +47us  score 88
[TOO CLOSE (saturated)      ] decode  70%(21/30)  recv 100%  mark 521(-39)sd9 sp0 712(+152)sd24 sp1 1840 compat-margin -75us  score 38
```

- **verdict** — `GOOD` / `TOO CLOSE (saturated)` / `TOO FAR / WEAK` /
  `MARGINAL (external RX may reject)` / `UNSTABLE (jitter)`.
- **decode %** — our decoder success over the rolling window.
- **recv %** — frames that produced any RX line (dropouts → too far).
- **mark / sp0 / sp1** — mean received widths (us) vs NEC nominal (560 / 560 /
  1690) with the signed deviation; `sd` is the jitter. Mark *short* + sp0
  *inflated* is the close-range saturation signature.
- **compat-margin** — headroom (us) of the worst-case (p90) zero-space before
  the **tightest external RX** (IRremoteESP8266, zero ceiling ≈ 637 us) rejects
  it. Negative means an external receiver would likely drop frames even if our
  own decoder still accepts them.
- **score 0–100** — `60·decode_rate + 25·compat_margin_term + 15·stability`.

Read it as: chase a high score *and* a positive compat-margin. If the verdict is
`TOO CLOSE`, separate the boards; if `TOO FAR / WEAK`, move them closer or
reduce angle/obstruction.

## Run

From `tests/`:

```sh
# first time (or after editing the sketches): build + upload both boards
uv run python hardware/link_quality/monitor.py --flash

# subsequent runs (boards already flashed): just connect and measure
uv run python hardware/link_quality/monitor.py
```

Options: `--window N` (rolling frames, default 30), `--interval S` (seconds
between sends, default 0.2), `--bits HEX` (NEC payload, default `cb3400ff`),
`--no-color`.

Ports and GPIOs come from `tests/.env`
(`TEST_SERIAL_PORT_RX_ESP32S3`, `TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3`,
`TEST_IR_RX_GPIO/INVERTED`, `TEST_IR_TX_GPIO/INVERTED`).

## Layout

```text
rx/        ESP32IRPulseKit receiver; dumps RX_DECODE/RX_RAW + raw ticks
tx/        ESP32IRPulseKit transmitter; sends NEC on "SEND NEC <hex>"
monitor.py host live dashboard + scoring (all metric logic lives here)
```
