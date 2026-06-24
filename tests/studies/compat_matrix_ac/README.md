# Hardware Compat Matrix (Air Conditioner)

> Japanese: [README.ja.md](README.ja.md)

`compat_matrix_ac/` compares ESP32IRPulseKit's air-conditioner layer
(`esp32irpk::ac`) against external AC libraries. It is separate from
`compat_matrix/` because the axis is different: the generic matrix compares
protocol **bits / timing / bit-order**, while this one compares decoded
**vendor state fields** (power / mode / temperature / fan).

Its main job is to **calibrate the PROVISIONAL Panasonic field map** in
`src/ac/Panasonic.h` (which byte/bit holds each field, and the per-vendor
mode/fan codes). The frame mechanics (Kaseikyo two-frame layout, timing, sum
checksum) are implemented from the documented format; this rig confirms the
field map against independent references without requiring a physical remote.

## Reference libraries

| Library | Direction | Role |
|---|---|---|
| IRremoteESP8266 (`IRPanasonicAc` / `IRac`) | decode + encode | Bidirectional cross-check |
| HeatpumpIR (`PanasonicHeatpumpIR`) | encode only | Second, independent TX reference |

Two independent encoders that agree (and that our decoder agrees with) confirm
the field map even when no real Panasonic remote is on hand.

## Variants

Same `<extlib>_<role>` convention as `compat_matrix/`: the primary sketch is the
DUT, `peer_tx/` is the transmitter, and the peer name stays `tx` so variants
reuse `TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3`.

```text
irremoteesp8266_tx/    # TX: IRremoteESP8266 (known state) -> RX: ESP32IRPulseKit  (calibrate our decode)
irremoteesp8266_rx/    # TX: ESP32IRPulseKit -> RX: IRremoteESP8266                (verify our encode)
irremoteesp8266_self/  # TX + RX: IRremoteESP8266                                 (reference baseline)
heatpumpir_tx/         # TX: HeatpumpIR (known state) -> RX: ESP32IRPulseKit       (second reference)
```

`irremoteesp8266_self` (IRremoteESP8266 encodes a known AC state and decodes its
own transmission) is the baseline: it confirms the reference round-trips in the
physical rig/placement before the cross directions are trusted.

Arduino-IRremote is intentionally **not** used here: it has no air-conditioner
state decoder (it reads Panasonic only as a generic 48-bit frame, not the AC
state), so it cannot compare fields.

- `*_tx` (external transmitter, our RX): the peer sends a **known** AC state
  (e.g. cool / 26 C / fan auto); our RX captures RAW (RAW-only +
  `setMaxRxSymbols` + large idle), decodes with `ac::Panasonic::Frame::fromRaw`,
  and prints the fields. The study records `sent state -> our decoded fields`,
  which is exactly what fixes the field map.
- `irremoteesp8266_rx` (our transmitter, external RX): our `ac::Panasonic`
  encodes a state; IRremoteESP8266 decodes it. Confirms our encoder.

## Serial format

The ESP32IRPulseKit RX primary prints one line per received AC burst:

```text
AC_DECODE vendor=PANASONIC checksum=ok power=1 mode=1 temp=26 fan=0 bytes=0220e0...
```

- `mode` / `fan` are the raw `ac::Panasonic::Mode` / `Fan` underlying values
  (a by-name helper is a later addition; SPEC §11.2). The study maps the known
  sent state to whatever values our decoder reports.
- `checksum` is `ok` / `bad` from `Frame::checksum_ok`.
- `bytes` is the full 27-byte state in hex, so a human can diff layouts.

External RX sketches print the reference library's own decode (vendor + fields)
in the same `AC_DECODE` shape.

## Run

Board study; not auto-collected (file is `study_*.py`). Needs two ESP32-S3
boards and the local serial ports / GPIOs in `.env`.

```sh
uv run --env-file .env pytest -s -o python_files="study_*.py" \
  studies/compat_matrix_ac/irremoteesp8266_tx/
```

## Status

The design and serial contract are fixed here; the per-variant sketches and
harness are added one variant at a time. In place:

- `irremoteesp8266_self/` -- baseline: confirms the physical rig round-trips a
  Panasonic A/C frame (IRremoteESP8266 encodes, transmits, receives, decodes
  back to the same 27 bytes) before the cross directions are trusted.
- `irremoteesp8266_tx/` -- field-map calibration: IRremoteESP8266 transmits a
  known state, our RX captures it RAW and decodes with `esp32irpk::ac::Panasonic`.
  Byte equality against the canonical frame is the hard pass/fail; per-field
  comparison against the known state is reported (not asserted) to drive the
  field map.
- `irremoteesp8266_rx/` -- encoder verification (complement of `_tx`): our TX
  encodes a known state with `esp32irpk::ac::Panasonic` and transmits it;
  IRremoteESP8266 decodes it. The bytes the external library recovers must equal
  the bytes our encoder produced, with a valid checksum -- proving our `toRaw()`
  emits a well-formed burst an independent stack accepts.

`heatpumpir_tx/` follows. Findings and the confirmed
field map are recorded back here and into `src/ac/Panasonic.h`.

The `irremoteesp8266_tx/` run confirms the Panasonic field map in
`src/ac/Panasonic.h` matches IRremoteESP8266's `IRPanasonicAc` byte-for-byte:
our RAW capture reproduces the canonical 27 bytes, and power / mode (auto, cool,
heat, dry) / temperature / fan all decode to the expected values. The fan nibble
is the Panasonic speed plus 3 (min/low/med/high/max = 0x3..0x7, auto = 0xA).

The `irremoteesp8266_rx/` run additionally confirms our encoder: a frame built
from setters and rendered by `toRaw()` is the full canonical state, including
the fixed feature bytes a real remote always carries ([15]=0x80, [19]=0x0E,
[20]=0xE0, [23]=0x81). A `Frame` therefore defaults to a known-good template so
those bytes are present even when only mode/temp/fan/power are set.

### Carrier reliability (long-frame TX)

With the same IRremoteESP8266 receiver and the same frames, only the transmitter
differs between two runs: IRremoteESP8266's own TX delivered 25/25 frames, while
our TX on the free-running hardware carrier (`setPhaseAlignedCarrier(false)`,
required for frames this long -- SPEC 11.3) delivered 23/25. Every frame that
arrived was byte-exact (no corruption); the two misses were whole-frame drops.
The gap points to the hardware carrier being marginally less reliable than a
phase-aligned carrier for long frames. Mitigations via carrier duty or repeat
are out of scope here; proper phase-aligned (or live-encoded) carrier support
for long frames is future work.
