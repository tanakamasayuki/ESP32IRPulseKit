# Hardware Compat Matrix

> Japanese: [README.ja.md](README.ja.md)

`compat_matrix/` is for compatibility and difference investigation against external libraries or alternate implementations. Multi-protocol ESP32IRPulseKit TX -> ESP32IRPulseKit RX checks live in `hardware/protocol_matrix/`.

Each test directory keeps the primary sketch as RX and `peer_tx/` as TX. Keeping the peer name fixed as `tx` lets variants reuse `TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3`.

Variants:

```text
irremoteesp8266_tx/     # RX: ESP32IRPulseKit, TX: IRremoteESP8266  (template)
irremoteesp8266_rx/     # RX: IRremoteESP8266, TX: ESP32IRPulseKit  (planned)
arduino_irremote_tx/    # RX: ESP32IRPulseKit, TX: Arduino-IRremote (planned)
arduino_irremote_rx/    # RX: Arduino-IRremote, TX: ESP32IRPulseKit (planned)
```

[`irremoteesp8266_tx/`](irremoteesp8266_tx/) is the first template. Other
variants follow the same structure.

`compat_matrix` is optional. Use it to observe score, raw_len, decode results, raw timing variation, and bit-order/field interpretation differences.

