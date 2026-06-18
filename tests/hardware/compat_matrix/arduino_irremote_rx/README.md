# Compat Matrix: Arduino-IRremote RX

> Japanese: [README.ja.md](README.ja.md)

RX: [Arduino-IRremote](https://github.com/Arduino-IRremote/Arduino-IRremote), TX: ESP32IRPulseKit.

This variant transmits with ESP32IRPulseKit and receives with Arduino-IRremote
`IrReceiver`. The peer TX (`peer_tx/`) is the same ESP32IRPulseKit sketch used
by `hardware/protocol_matrix/`; only the primary RX sketch swaps to the external
library.

- Primary sketch (`arduino_irremote_rx.ino`): RX, prints `RX_READY impl=Arduino-IRremote ...`
- `peer_tx/peer_tx.ino`: TX, prints `TX_READY impl=ESP32IRPulseKit ...`, exposed as `peers["tx"]`

Only `DECODE_NEC` / `DECODE_SONY` / `DECODE_SAMSUNG` / `DECODE_JVC` are enabled.
Arduino-IRremote has no "score" metric, so the RX reports `score=0`. Protocol
names are mapped to the in-house uppercase form (NEC / SONY<bits> /
SAMSUNG<bits> / JVC<bits>). Arduino-IRremote assumes a standard active-low IR
receiver module and has no inverted-input option, so `IR_RX_INVERTED` is
reported but not applied.

## Cases

- NEC
- SONY12
- SONY15
- SONY20
- SAMSUNG32
- SAMSUNG36
- JVC

## Run

```sh
cd tests
uv run --env-file .env pytest hardware/compat_matrix/arduino_irremote_rx/
```

pytest prints `COMPAT_MATRIX_OBSERVED` with the sent protocol/bits and the
RX-observed protocol, bits, raw_len, and `bit_order` (same / reversed / other).
Bit order and field interpretation can differ between implementations, so the
test does **not** assert an exact bits match. It only requires that RX
recognized a frame (`raw_len > 0`); the differences are recorded as observations.

## Known Failures

In the 2026-06-18 hardware log
`/tmp/pytest-embedded/2026-06-18_07-31-59-959191`, these cases failed. They are
kept as normal pytest failures; this section tracks whether the reason is an
external-RX coverage limit or something PulseKit should adjust.

| Case | Observation | Assessment |
|---|---|---|
| SAMSUNG36 | `RX_RAW len=76`; the frame is received but Arduino-IRremote does not decode it | Arduino-IRremote 4.7.1 covers Samsung32/Samsung48, but no dedicated Samsung36 decoder was found. Treat as outside this external RX's coverage |
| JVC | (pending re-measurement) | Now a standard 16-bit JVC frame; expected to interoperate with the external library's 16-bit JVC. Re-run to confirm. |
