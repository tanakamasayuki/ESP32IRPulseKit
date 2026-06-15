# Hardware Automated Tests

> Japanese: [README.ja.md](README.ja.md)

ESP32 hardware tests will live here.

The standard setup uses two boards.

- TX board: sends known RAW/BITS with `IRSender`.
- RX board: receives with `IRReceiver` and prints decoded results to Serial.
- pytest: controls both serial ports and checks that the received protocol/bits match the transmitted command.

The standard automated hardware target is a two-board ESP32-S3 setup for now. Other SoCs such as ESP32 classic and ESP32-C3/C6 are checked through `examples/` and manual runs, then promoted to optional profiles only when needed.

Single-board loopback is auxiliary. Direct GPIO loopback can change inversion conditions compared with a real IR receiver module, so it is not the primary pass/fail baseline.

The peer board does not initially use a separate reference IR library. Baseline signal data lives under `tests/fixtures/`, using both `protocol + bits` commands and fixed `raw_ticks`.

GPIO numbers and inversion settings are environment-specific and are configured through `.env`. The two-board setup uses `TEST_IR_TX_GPIO`, `TEST_IR_RX_GPIO`, `TEST_IR_TX_INVERTED`, and `TEST_IR_RX_INVERTED`.
