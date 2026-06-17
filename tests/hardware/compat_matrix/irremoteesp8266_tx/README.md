# Compat Matrix: IRremoteESP8266 TX

> Japanese: [README.ja.md](README.ja.md)

RX: ESP32IRPulseKit, TX: [IRremoteESP8266](https://github.com/crankyoldgit/IRremoteESP8266).

This variant drives the IR LED with IRremoteESP8266 and decodes the result with
ESP32IRPulseKit `IRReceiver`. It reuses the same serial protocol as
`hardware/protocol_matrix/`, so the only change from the in-house TX is the peer
sketch under `peer_tx/`.

- Primary sketch (`irremoteesp8266_tx.ino`): RX, prints `RX_READY impl=ESP32IRPulseKit ...`
- `peer_tx/peer_tx.ino`: TX, prints `TX_READY impl=IRremoteESP8266 ...`, exposed as `peers["tx"]`

## Cases

- NEC
- SONY12
- SAMSUNG32
- JVC24

## Run

```sh
cd tests
uv run --env-file .env pytest hardware/compat_matrix/irremoteesp8266_tx/
```

pytest prints `COMPAT_MATRIX_OBSERVED` with the sent protocol/bits and the
RX-observed protocol, bits, score, and raw_len. Bit order and field
interpretation can differ between implementations, so the test does **not**
assert an exact bits match. It only requires that RX recognized a frame
(`raw_len > 0`); the protocol/bits differences are recorded as observations
(see `bits_match`).
