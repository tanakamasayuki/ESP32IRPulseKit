# Hardware Automated Tests

ESP32 hardware tests will live here.

The standard setup uses two boards.

- TX board: sends known RAW/BITS with `IRSender`.
- RX board: receives with `IRReceiver` and prints decoded results to Serial.
- pytest: controls both serial ports and checks that the received protocol/bits match the transmitted command.

Single-board loopback is auxiliary. Direct GPIO loopback can change inversion conditions compared with a real IR receiver module, so it is not the primary pass/fail baseline.

The peer board does not initially use a separate reference IR library. Baseline signal data lives under `tests/fixtures/`, using both `protocol + bits` commands and fixed `raw_ticks`.
