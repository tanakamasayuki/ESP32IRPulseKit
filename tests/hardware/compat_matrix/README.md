# Hardware Compat Matrix

> Japanese: [README.ja.md](README.ja.md)

`compat_matrix/` is for compatibility and difference investigation against external libraries or alternate implementations. Multi-protocol ESP32IRPulseKit TX -> ESP32IRPulseKit RX checks live in `hardware/protocol_matrix/`.

Each test directory keeps the primary sketch as RX and `peer_tx/` as TX. Keeping the peer name fixed as `tx` lets variants reuse `TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3`.

Variants:

```text
irremoteesp8266_tx/     # RX: ESP32IRPulseKit, TX: IRremoteESP8266
irremoteesp8266_rx/     # RX: IRremoteESP8266, TX: ESP32IRPulseKit
arduino_irremote_tx/    # RX: ESP32IRPulseKit, TX: Arduino-IRremote
arduino_irremote_rx/    # RX: Arduino-IRremote, TX: ESP32IRPulseKit
```

All four variants follow the same structure. `*_tx` variants keep the
ESP32IRPulseKit RX sketch and swap the `peer_tx/` transmitter to an external
library; `*_rx` variants keep the ESP32IRPulseKit `peer_tx/` transmitter and
swap the primary receiver to an external library. Each test records
`bit_order` (same / reversed / other) alongside the observed bits, since
implementations often differ only by MSB/LSB-first integer representation.

`compat_matrix` is optional. Use it to observe score, raw_len, decode results, raw timing variation, and bit-order/field interpretation differences.

