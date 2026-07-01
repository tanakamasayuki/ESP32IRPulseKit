# Hardware Automated Tests

> Japanese: [README.ja.md](README.ja.md)

ESP32 hardware tests live here.

## Directories

| Path | Purpose | Release gate |
| --- | --- | --- |
| `link_smoke/` | Stable smoke test that proves the library's two-board IR link works | Required |
| `protocol_matrix/` | Multi-protocol hardware matrix for ESP32IRPulseKit TX -> ESP32IRPulseKit RX | Release-gate candidate |
| `protocol_matrix_ac/` | Air-conditioner self round-trip matrix (`ac::` layer, TX -> RX, one state per vendor) | Release-gate candidate |

These produce a clear pass/fail and are auto-collected by `pytest hardware`. Board investigations that only record observation logs (jitter, timing sweeps, external-library compatibility) live under `tests/studies/` and are not auto-collected.

`protocol_matrix/` keeps the primary sketch as RX and `peer_tx/` as TX. Keeping the peer name fixed as `tx` lets all variants reuse `TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3`.

The standard setup uses two boards.

- TX board: sends known RAW/BITS with `IRSender`.
- RX board: receives with `IRReceiver` and prints decoded results to Serial.
- pytest: controls both serial ports and checks that the received protocol/bits match the transmitted command.

The standard automated hardware target is a two-board ESP32-S3 setup for now. Other SoCs such as ESP32 classic and ESP32-C3/C6 are checked through `examples/`, then promoted to optional profiles only when needed.

Single-board loopback is auxiliary. Direct GPIO loopback can change inversion conditions compared with a real IR receiver module, so it is not the primary pass/fail baseline.

`link_smoke/` does not use a separate reference IR library. Baseline signal data lives under `tests/pc/fixtures/`, using both `protocol + bits` commands and fixed `raw_ticks`.

GPIO numbers and inversion settings are environment-specific and are configured through `.env`. The two-board setup uses `TEST_IR_TX_GPIO`, `TEST_IR_RX_GPIO`, `TEST_IR_TX_INVERTED`, and `TEST_IR_RX_INVERTED`.
