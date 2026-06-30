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
# Panasonic
panasonic_irremoteesp8266_tx/        # TX: IRremoteESP8266 (known state) -> RX: ESP32IRPulseKit  (calibrate our decode)
panasonic_irremoteesp8266_rx/        # TX: ESP32IRPulseKit -> RX: IRremoteESP8266                (verify our encode)
panasonic_irremoteesp8266_self/      # TX + RX: IRremoteESP8266                                 (reference baseline)
panasonic_heatpumpir_tx/             # TX: HeatpumpIR (known state) -> RX: ESP32IRPulseKit       (second reference)

# Gree (IRGreeAC with the YBOFB model, matching esp32irpk::ac::Gree)
gree_irremoteesp8266_tx/   # TX: IRremoteESP8266 (known state) -> RX: ESP32IRPulseKit  (calibrate our decode)
gree_irremoteesp8266_rx/   # TX: ESP32IRPulseKit -> RX: IRremoteESP8266                (verify our encode)

# Fujitsu (IRFujitsuAC with the ARRAH2E model, matching esp32irpk::ac::Fujitsu)
fujitsu_irremoteesp8266_tx/  # TX: IRremoteESP8266 (known state) -> RX: ESP32IRPulseKit  (calibrate our decode)
fujitsu_irremoteesp8266_rx/  # TX: ESP32IRPulseKit -> RX: IRremoteESP8266                (verify our encode)

# Daikin (IRDaikinESP, classic ARC433, matching esp32irpk::ac::Daikin)
daikin_irremoteesp8266_tx/   # TX: IRremoteESP8266 (known state) -> RX: ESP32IRPulseKit  (calibrate our decode)
daikin_irremoteesp8266_rx/   # TX: ESP32IRPulseKit -> RX: IRremoteESP8266                (verify our encode)

# Toshiba (IRToshibaAC, standard 9-byte TOSHIBA_AC, matching esp32irpk::ac::Toshiba)
toshiba_irremoteesp8266_tx/  # TX: IRremoteESP8266 (known state) -> RX: ESP32IRPulseKit  (calibrate our decode)
toshiba_irremoteesp8266_rx/  # TX: ESP32IRPulseKit -> RX: IRremoteESP8266                (verify our encode)

# Samsung (IRSamsungAc, standard 14-byte SAMSUNG_AC, matching esp32irpk::ac::Samsung)
samsung_irremoteesp8266_tx/  # TX: IRremoteESP8266 (known state) -> RX: ESP32IRPulseKit  (calibrate our decode)
samsung_irremoteesp8266_rx/  # TX: ESP32IRPulseKit -> RX: IRremoteESP8266                (verify our encode)
```

Each vendor uses the same `<extlib>_<role>` variants. Every variant folder is
prefixed with its vendor name (`panasonic_…`, `gree_…`).

`panasonic_irremoteesp8266_self` (IRremoteESP8266 encodes a known AC state and decodes its
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
- `panasonic_irremoteesp8266_rx` (our transmitter, external RX): our `ac::Panasonic`
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
  studies/compat_matrix_ac/panasonic_irremoteesp8266_tx/
```

## Status

The design and serial contract are fixed here; the per-variant sketches and
harness are added one variant at a time. In place:

- `panasonic_irremoteesp8266_self/` -- baseline: confirms the physical rig round-trips a
  Panasonic A/C frame (IRremoteESP8266 encodes, transmits, receives, decodes
  back to the same 27 bytes) before the cross directions are trusted.
- `panasonic_irremoteesp8266_tx/` -- field-map calibration: IRremoteESP8266 transmits a
  known state, our RX captures it RAW and decodes with `esp32irpk::ac::Panasonic`.
  Byte equality against the canonical frame is the hard pass/fail; per-field
  comparison against the known state is reported (not asserted) to drive the
  field map. `test_irremoteesp8266_tx_models` additionally has the peer encode
  each Panasonic model (`setModel` DKE/NKE/LKE/RKR) and checks our RX recovers
  the canonical bytes AND self-identifies the same model (`Frame::model`), so the
  per-model marker bytes and detection are confirmed against IRremoteESP8266.
- `panasonic_irremoteesp8266_rx/` -- encoder verification (complement of `_tx`): our TX
  encodes a known state with `esp32irpk::ac::Panasonic` and transmits it;
  IRremoteESP8266 decodes it. The bytes the external library recovers must equal
  the bytes our encoder produced, with a valid checksum -- proving our `toRaw()`
  emits a well-formed burst an independent stack accepts.
- `panasonic_heatpumpir_tx/` -- second, independent reference: HeatpumpIR
  (`PanasonicJKEHeatpumpIR`, a separate codebase, over an LEDC carrier)
  transmits a known state; our RX decodes it. The hard check is that our decode
  is checksum-valid and its logical fields match the sent state (not byte
  identity -- the value is two independent encoders agreeing on field
  semantics). HeatpumpIR's fan steps are offset by one, so this variant also
  covers the QUIET and POWERFUL fan settings the IRremoteESP8266 peer cannot
  reach.

- `gree_irremoteesp8266_tx/` + `gree_irremoteesp8266_rx/` -- the same calibration
  and encoder-verification pair for Gree, using IRremoteESP8266's `IRGreeAC` with
  the YBOFB model (so the model bit stays clear and byte 2 is a stable 0x20,
  matching `esp32irpk::ac::Gree`). The `tx` run confirms our RAW decode of the
  two-block frame (second block has no header) reproduces the canonical 8 bytes
  byte-for-byte; the `rx` run confirms our encoder emits, and an independent stack
  accepts, those bytes with a valid Kelvinator block checksum. Two Gree specifics:
  (1) its RX primary needs a 50 ms end-of-message timeout — `decodeGree` reads the
  ~20 ms inter-block gap as block 2's header space, so a thinner margin splits the
  second block off; (2) Gree must be sent with the phase-aligned carrier — the
  free-running hardware carrier's ~1-cycle mark wobble drops about half the frames
  (its 540 µs zero-space is shorter than the 620 µs bit mark, so the wobble pushes
  spaces out of tolerance; measured phase-aligned 50/50 vs hardware ~55%, see
  `study_gree_carrier_ab.py`). Even on the phase-aligned carrier the 540 µs zero-space
  leaves little headroom against a receiver that lengthens spaces, so the IR path
  must be reasonably aligned for the reference decoder to accept it at its default
  tolerance.

- `mitsubishi_irremoteesp8266_tx/` + `mitsubishi_irremoteesp8266_rx/` -- the same
  calibration and encoder-verification pair for the 18-byte "Mitsubishi AC"
  protocol, using IRremoteESP8266's `IRMitsubishiAC`. The frame is a single
  pulse-distance frame (5-byte signature, sum checksum in the last byte) sent
  twice with a ~15.5 ms gap; the `rx` primary uses a 50 ms timeout so both copies
  land in one capture. Like Gree, Mitsubishi's zero-space (420 µs) is shorter than
  its bit mark (450 µs), so it is sent on the phase-aligned carrier (the `rx`
  study sets it; `study_mitsubishi_carrier_ab.py` quantifies phase-aligned vs hardware).

- `fujitsu_irremoteesp8266_tx/` + `fujitsu_irremoteesp8266_rx/` -- the same
  calibration and encoder-verification pair for the "Fujitsu AC" protocol (model
  ARRAH2E), using IRremoteESP8266's `IRFujitsuAC`. A full setting is a 16-byte
  long frame (fixed `14 63 00 10 10`, byte 5 = `0xFE`, complement checksum in byte
  15); a power-off is a 7-byte short frame (`14 63 00 10 10 02 FD`), so the cases
  include a `power=0` case that exercises the short frame. The frame is sent once;
  the `rx` primary uses a 50 ms timeout. Like Gree/Mitsubishi, Fujitsu's zero-space
  (390 µs) is shorter than its bit mark (448 µs), so it is sent on the
  phase-aligned carrier. Our `esp32irpk::ac::Fujitsu` enum values equal the
  IRremoteESP8266 wire codes, so the field comparison is a direct numeric match.

- `daikin_irremoteesp8266_tx/` + `daikin_irremoteesp8266_rx/` -- the same calibration
  and encoder-verification pair for the classic "Daikin" / ARC433 protocol, using
  IRremoteESP8266's `IRDaikinESP`. The 35-byte state is sent as a 5-bit `00000`
  preamble followed by three pulse-distance sections (8 / 8 / 19 bytes), each with
  its own `3650/1623 µs` header and a per-section sum checksum (bytes 7 / 15 / 34),
  every section beginning with the `11 DA 27` signature; the `tx` primary skips the
  preamble and decodes the three sections, the `rx` peer renders preamble + sections
  via `toRaw`. The `rx` primary uses a 65 ms timeout so all sections (separated by
  ~29 ms gaps) land in one capture. Daikin is the tightest-timing vendor (zero-space
  428 µs == bit mark 428 µs), so it is sent on the phase-aligned carrier. Our
  `esp32irpk::ac::Daikin` enum values equal the IRremoteESP8266 wire codes, so the
  field comparison is a direct numeric match.

- `toshiba_irremoteesp8266_tx/` + `toshiba_irremoteesp8266_rx/` -- the same calibration
  and encoder-verification pair for the standard 9-byte TOSHIBA_AC protocol, using
  IRremoteESP8266's `IRToshibaAC`. A single **MSB-first** frame (the only MSB-first
  AC vendor) with the `F2 0D` signature and an XOR checksum in byte 8; power is the
  Mode field's off code (7). Exercises the codec's MSB-first path on hardware.

- `samsung_irremoteesp8266_tx/` + `samsung_irremoteesp8266_rx/` -- the same calibration
  and encoder-verification pair for the standard 14-byte SAMSUNG_AC protocol, using
  IRremoteESP8266's `IRSamsungAc`. LSB-first, a one-time leading header (690/17844 µs)
  then two 7-byte sections, each with a popcount section checksum split across its
  bytes 1-2; power is two 2-bit fields. The TX peer drives `IRsend::sendSamsungAC`
  directly (not `IRSamsungAc::send`, which emits a 21-byte extended frame on power
  changes), and the RX uses a 30 ms timeout to span the 17.8 ms leading gap. There is
  **no** `samsung_heatpumpir_tx`: HeatpumpIR's Samsung classes implement the older AQV
  (21-byte) and FJM (different section-2 checksum) variants, neither matching the
  modern 14-byte format, so Samsung is cross-checked by the IRremoteESP8266
  bidirectional pair alone.

- `gree_heatpumpir_tx/` + `mitsubishi_heatpumpir_tx/` + `fujitsu_heatpumpir_tx/` + `daikin_heatpumpir_tx/` + `toshiba_heatpumpir_tx/` -- a second independent
  reference for each, mirroring `panasonic_heatpumpir_tx`: HeatpumpIR (`GreeGenericHeatpumpIR`
  / `MitsubishiFEHeatpumpIR` / `FujitsuHeatpumpIR` / `DaikinHeatpumpIR`, a separate
  codebase over an LEDC carrier) transmits a known state and our RX decodes it. The
  hard check is semantic (checksum valid + logical fields match the sent state), not
  byte identity, since HeatpumpIR fills different auxiliary bytes than
  IRremoteESP8266. HeatpumpIR's Mitsubishi fan steps reach QUIET and HIGH, and it
  encodes fan-auto without the dedicated FanAuto bit, so that variant also exercises
  those decode paths. HeatpumpIR's Fujitsu emits the same ARRAH2E long frame (byte 5
  = 0xFE) with a checksum that reduces to the same value as ours, and its fan
  constants are inverted vs the wire codes (FAN_1 = quiet .. FAN_4 = high), so its
  peer maps each speed token to the FAN_x that yields the intended wire code.
  HeatpumpIR's Daikin emits the same classic 35-byte / 3-section ARC433 frame; it
  keeps the template's section-1/2 checksums and recomputes only byte 34, so our
  three-checksum validation passes. Its Daikin encoder does not drive swing (fixed
  off) and has no quiet step, so that variant calibrates power / mode / temp / fan.
  HeatpumpIR's Toshiba stores the state bit-reversed and sends it, so on the wire it
  is the same standard MSB-first TOSHIBA_AC frame our decoder reads directly; it has
  no FAN operating mode and does not drive swing, so that variant calibrates
  power / mode (auto/cool/dry/heat) / temp / fan.

Findings and the confirmed field maps are recorded back here and into
`src/ac/Panasonic.h` / `src/ac/Gree.h` / `src/ac/Mitsubishi.h` / `src/ac/Fujitsu.h` / `src/ac/Daikin.h` / `src/ac/Toshiba.h` / `src/ac/Samsung.h`.

The `panasonic_irremoteesp8266_tx/` run confirms the Panasonic field map in
`src/ac/Panasonic.h` matches IRremoteESP8266's `IRPanasonicAc` byte-for-byte:
our RAW capture reproduces the canonical 27 bytes, and power / mode (auto, cool,
heat, dry) / temperature / fan all decode to the expected values. The fan nibble
is the Panasonic speed plus 3 (min/low/med/high/max = 0x3..0x7, auto = 0xA).

The `panasonic_irremoteesp8266_rx/` run additionally confirms our encoder: a frame built
from setters and rendered by `toRaw()` is the full canonical state, including
the fixed feature bytes a real remote always carries ([15]=0x80, [19]=0x0E,
[20]=0xE0, [23]=0x81). A `Frame` therefore defaults to a known-good template so
those bytes are present even when only mode/temp/fan/power are set.

### Carrier reliability (long-frame TX)

The AC examples select the hardware carrier with `setPhaseAlignedCarrier(false)`
(the library carrier is otherwise phase-aligned). The phase-aligned carrier can
give higher mark precision but expands a long AC burst to ~17 KB of symbols per
send and raises the refill-underrun risk under heavy interrupt load (SPEC 11.3),
which is why the examples recommend the hardware carrier for long frames.

`study_panasonic_irremoteesp8266_carrier_ab.py` measures whether that choice costs delivery. The
`panasonic_irremoteesp8266_rx` peer (our TX) takes a runtime `CARRIER pa` / `CARRIER hw`
command (build-time default `PULSEKIT_CARRIER`, 0 = hardware), and the study sends
each state under both modes and records the canonical-delivery rate per mode. On
the test rig both carriers delivered every frame (phase-aligned and hardware each
150/150, no corruption), so there is no measurable delivery difference here --
the phase-aligned symbol expansion did not hurt at default settings under test
load. (An earlier one-off 23/25 on the hardware carrier was transient RF
variance.) It is a measurement study, not a gate: it asserts only that each mode
opens and that the encoder stays canonical in both, so a degraded phase-aligned
rate would be reported, not failed. Run it with:

```sh
uv run --env-file .env pytest -s -o python_files="study_*.py" \
  studies/compat_matrix_ac/panasonic_irremoteesp8266_rx/study_panasonic_irremoteesp8266_carrier_ab.py
```

### Decoder tolerance (timing skew)

`panasonic_heatpumpir_tx` also surfaced a decoder-strictness issue. HeatpumpIR's ESP32
sender is a busy-loop bit-banger that re-attaches the LEDC carrier per mark, so
every space comes out ~150us long (a zero space is ~620us captured vs the 432us
nominal). The original per-bit decode used narrow windows around each 0/1 length,
which left a dead zone that rejected those frames outright -- stricter than a real
Panasonic A/C unit, which HeatpumpIR drives fine. The space classifier now uses
the midpoint threshold between the 0 and 1 lengths (and a separate, larger
frame-end threshold for the inter-frame gap), so it tolerates real-world sender
skew while integrity stays enforced by the checksum. A captured HeatpumpIR frame
is locked in as a host regression test (`testPanasonicAcDecodesSkewedTiming`).
