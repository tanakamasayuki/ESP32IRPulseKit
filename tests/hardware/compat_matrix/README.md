# Hardware Compat Matrix

> Japanese: [README.ja.md](README.ja.md)

`compat_matrix/` contains hardware-based compatibility and investigation tests. `hardware/link_smoke/` remains the stable release-gate smoke test; this tree is for protocol differences, bit order, raw timing variation, and external library comparisons.

Each test directory keeps the primary sketch as RX and `peer_tx/` as TX. Keeping the peer name fixed as `tx` lets tests reuse the existing `TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3` port setting.

## Layout

```text
compat_matrix/
  esp32irpk_self/       # RX: ESP32IRPulseKit, TX: ESP32IRPulseKit
    esp32irpk_self.ino
    peer_tx/
      peer_tx.ino
```

Future external-library tests should keep the same RX parent / TX peer layout.

```text
irremoteesp8266_tx/     # RX: ESP32IRPulseKit, TX: IRremoteESP8266
irremoteesp8266_rx/     # RX: IRremoteESP8266, TX: ESP32IRPulseKit
```

## Run

```sh
cd tests
uv run --env-file .env pytest hardware/compat_matrix/esp32irpk_self/
```

