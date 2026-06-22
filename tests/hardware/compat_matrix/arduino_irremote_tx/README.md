# Compat Matrix: Arduino-IRremote TX

> Japanese: [README.ja.md](README.ja.md)

RX: ESP32IRPulseKit, TX: [Arduino-IRremote](https://github.com/Arduino-IRremote/Arduino-IRremote).

This variant drives the IR LED with Arduino-IRremote and decodes the result with
ESP32IRPulseKit `IRReceiver`. It reuses the same serial protocol as
`hardware/protocol_matrix/`, so the only change from the in-house TX is the peer
sketch under `peer_tx/`.

- Primary sketch (`arduino_irremote_tx.ino`): RX, prints `RX_READY impl=ESP32IRPulseKit ...`
- `peer_tx/peer_tx.ino`: TX, prints `TX_READY impl=Arduino-IRremote ...`, exposed as `peers["tx"]`

Arduino-IRremote's modern send API is address/command oriented, so to transmit
an arbitrary raw payload the peer uses the deprecated MSB-first raw senders
(`sendNECMSB`/`sendSonyMSB`/`sendSamsungMSB`/`sendJVCMSB`). It also has no
inverted-output option, so `IR_TX_INVERTED` is reported but not applied.

## Cases

- NEC
- SONY12
- SONY15
- SONY20
- SAMSUNG32
- JVC

(SAMSUNG36 is excluded — Arduino-IRremote does not support Samsung36; it is
cross-tested only against IRremoteESP8266.)

## Run

```sh
cd tests
uv run --env-file .env pytest hardware/compat_matrix/arduino_irremote_tx/
```

pytest prints `COMPAT_MATRIX_OBSERVED` with the sent protocol/bits and the
RX-observed protocol, bits, score, raw_len, and `bit_order` (same / reversed /
other). Bit order and field interpretation can differ between implementations,
so the test does **not** assert an exact bits match. It only requires that RX
recognized a frame (`raw_len > 0`); the differences are recorded as observations.
