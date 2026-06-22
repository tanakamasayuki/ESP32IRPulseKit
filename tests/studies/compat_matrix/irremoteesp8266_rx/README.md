# Compat Matrix: IRremoteESP8266 RX

> Japanese: [README.ja.md](README.ja.md)

RX: [IRremoteESP8266](https://github.com/crankyoldgit/IRremoteESP8266), TX: ESP32IRPulseKit.

This variant transmits with ESP32IRPulseKit and receives with IRremoteESP8266
`IRrecv`. The peer TX (`peer_tx/`) is the same ESP32IRPulseKit sketch used by
`hardware/protocol_matrix/`; only the primary RX sketch swaps to the external
library.

- Primary sketch (`irremoteesp8266_rx.ino`): RX, prints `RX_READY impl=IRremoteESP8266 ...`
- `peer_tx/peer_tx.ino`: TX, prints `TX_READY impl=ESP32IRPulseKit ...`, exposed as `peers["tx"]`

IRremoteESP8266 has no "score" metric, so the RX reports `score=0`. Protocol
names are mapped to the in-house uppercase form (NEC / SONY<bits> /
SAMSUNG<bits> / JVC<bits>). IRremoteESP8266 `IRrecv` assumes a standard
active-low IR receiver module and has no inverted-input option, so
`IR_RX_INVERTED` is reported but not applied.

## Cases

- NEC
- SONY12
- SONY15
- SONY20
- SAMSUNG32
- SAMSUNG36
- JVC
- RC5
- RC6_M0_16

## Run

```sh
cd tests
uv run --env-file .env pytest -o python_files="study_*.py" studies/compat_matrix/irremoteesp8266_rx/
```

pytest prints `COMPAT_MATRIX_OBSERVED` with the sent protocol/bits and the
RX-observed protocol, bits, raw_len, and `bit_order` (same / reversed / other).
Bit order and field interpretation can differ between implementations, so the
test does **not** assert an exact bits match. It only requires that RX
recognized a frame (`raw_len > 0`); the differences are recorded as observations.
