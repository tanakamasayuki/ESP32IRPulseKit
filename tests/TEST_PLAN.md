# Test Plan

> Japanese: [TEST_PLAN.ja.md](TEST_PLAN.ja.md)

## Strategy

Tests are grouped into four levels.

| Type | Purpose | Environment |
| --- | --- | --- |
| host | Runtime assertions for codec, protocol specs, and frame conversion | Arduino host + pytest on a PC |
| build | ESP32 build checks for examples and minimal sketches | pytest + Arduino CLI on a PC |
| hardware | RMT TX/RX, GPIO inversion, idle threshold, queue/stat behavior | ESP32 boards + pytest-embedded |
| manual | Real remotes, distance/angle, ambient light, and human-observed behavior | Manual setup |

IR behavior is affected by the physical environment, so host tests assert RAW/BITS/Frame logic in an Arduino host environment. Build tests verify examples and public headers compile for ESP32. RMT-dependent behavior is verified by hardware tests.

## Run Policy

| Environment | Tests |
| --- | --- |
| Local development | host, hardware |
| GitHub Actions | host, build, fixtures |
| As needed | manual |

Do not run bare `pytest`. Always select a parent directory such as `host`, `build`, `fixtures`, or `hardware/tx_rx`. The `hardware/` tree depends on physical boards and local serial ports, so it is not part of CI.

## Initial Coverage

| Feature | host | build | hardware | manual | Status |
| --- | --- | --- | --- | --- | --- |
| NEC encode/decode roundtrip | ✅ | ✅ | ✅ NEC smoke | | Host/build/two-board smoke exist |
| NEC repeat encode/decode | ✅ | | ⬜ | | Host smoke tests exist |
| SONY decode | ✅ | | ⬜ | | Sony12 fixture host test; SONY15/20 pending |
| Samsung decode | ✅ | | ⬜ | | Samsung32 fixture host test; SAMSUNG36 pending |
| JVC decode | ✅ | | ⬜ | | JVC24 fixture host test; JVC32 pending |
| Panasonic decode | ✅ | | ⬜ | | Panasonic48 fixture host test; PANASONIC40 pending |
| AEHA variable-length encode/decode | ✅ | | ⬜ | | Host smoke + MSB-first variable test |
| RC5/RC6 decode | ✅ | | ⬜ | | RC5, RC6_M0, RC6_M6 fixture host tests |
| Candidate ordering and score threshold | ✅ | | | | Host smoke test exists |
| Encode rejection / invalid inputs | ✅ | | | | Undersized buffer, unknown id, bad length |
| RAW-only mode (0 candidates) | ✅ | | | | Host smoke test exists |
| Tolerance boundaries | ⬜ | | | | Planned |
| Verified fixture schema | ✅ | | | | YAML checks added |
| Examples build | | ⬜ | | | Planned in build tests |
| RMT TX RAW send | | ✅ sketch build | ✅ NEC smoke | | TX peer sketch + two-board smoke |
| RMT RX RAW receive | | ✅ sketch build | ✅ NEC smoke | | RX dut sketch + two-board smoke |
| TX->RX loop | | | ✅ NEC smoke | | Two-board pytest exists |
| Real remote receive | | | | ⬜ | Candidate for fixture promotion |

## Hardware Setup

The standard hardware test setup uses two boards.

- TX board: sends known `protocol + bits` commands or `raw_ticks`
- RX board: receives with `IRReceiver` and prints decoded results to Serial
- pytest: controls both serial ports and asserts expected protocol/bits/score
- GPIO/inversion settings: injected through `build_config.toml` from `.env` keys `TEST_IR_TX_GPIO`, `TEST_IR_RX_GPIO`, `TEST_IR_TX_INVERTED`, and `TEST_IR_RX_INVERTED`

The standard automated hardware target is a **two-board ESP32-S3 setup** for now. ESP32 classic, ESP32-C3/C6, and other SoCs are checked first with `examples/` and manual runs. If a SoC-specific difference or bug appears, it can be promoted to an optional profile or manual test.

Single-board loopback is auxiliary. Direct GPIO loopback can change the inversion conditions compared with a real IR receiver module, so it is not the primary pass/fail baseline.

The peer board does not initially use a separate reference IR library. Baselines are kept in this order:

1. `protocol + bits` generated from protocol specs
2. Hand-written or reviewed fixed `raw_ticks`
3. Captured real-world fixtures promoted after review
4. External library comparisons only when compatibility testing is needed

## Signal Data

IR signal data lives under `tests/fixtures/`.

- `generated/`: ideal waveforms generated from protocol specs and BITS
- `verified/`: hand-written or reviewed fixed RAW data
- `captured/`: unreviewed RAW captured from real remotes or hardware tests

Hardware tests distinguish two send modes.

- `SEND protocol bits`: tests the integrated `IRSender` and `IRReceiver` path
- `SEND_RAW raw_ticks`: tests decode behavior against known waveforms

## Priority

1. Select the Arduino host runner and add host runtime tests.
2. Add build tests for examples and minimal sketches.
3. Add two-board TX/RX hardware tests.
4. Promote captured real remote RAW fixtures into host/hardware tests.
