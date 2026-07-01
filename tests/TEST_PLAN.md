# Test Plan

> Japanese: [TEST_PLAN.ja.md](TEST_PLAN.ja.md)

## Strategy

Tests are grouped into three levels.

| Type | Purpose | Environment |
| --- | --- | --- |
| pc/fixtures | Signal-data schema and formula checks | pytest on a PC |
| pc/codec_smoke | Runtime assertions for codec, protocol specs, and frame conversion | Arduino host + pytest on a PC |
| pc/compile | ESP32 build-only checks for examples and minimal sketches | pytest + Arduino CLI on a PC |
| hardware | RMT TX/RX pass/fail regression on two boards | ESP32 boards + pytest-embedded |
| studies | On-demand board investigations that record observation logs | ESP32 boards + pytest (human analysis) |

IR behavior is affected by the physical environment, so `pc/codec_smoke` asserts RAW/BITS/Frame logic in an Arduino host environment. `pc/compile` verifies examples and public headers compile for ESP32. RMT-dependent behavior is verified by hardware tests.

## Run Policy

| Environment | Tests |
| --- | --- |
| Local development | pc, hardware/link_smoke, hardware/protocol_matrix, hardware/protocol_matrix_ac |
| GitHub Actions | pc (fixtures, codec_smoke, compile) |
| As needed | studies |

Select a top-level folder such as `pc` or `hardware/link_smoke`. Files under `studies/` are named `study_*.py`, so they are never auto-collected; run a study on demand with `-o python_files="study_*.py"`. The `hardware/` and `studies/` trees depend on physical boards and local serial ports, so they are not part of CI.

## Initial Coverage

| Feature | codec_smoke | compile | hardware | Status |
| --- | --- | --- | --- | --- |
| NEC encode/decode roundtrip | ✅ | ✅ | ✅ NEC smoke | Host/build/two-board smoke exist |
| NEC repeat encode/decode | ✅ | | ⬜ | Host smoke tests exist |
| SONY decode | ✅ | | ⬜ | Sony12 fixture host test; SONY15/20 generated roundtrips and formula checks exist |
| Samsung decode | ✅ | | ⬜ | Samsung32 fixture host test; SAMSUNG36 generated roundtrip and formula check exist |
| JVC decode | ✅ | | ⬜ | JVC fixture host test; encode/decode roundtrip and formula check exist |
| AEHA variable-length encode/decode | ✅ | | ⬜ | Host smoke + MSB-first variable test (covers Kaseikyo/Panasonic) |
| RC5/RC6 decode | ✅ | | ⬜ | RC5, RC6_M0, RC6_M6 fixture host tests |
| Protocol carrier preferences | ✅ | ✅ | ✅ NEC smoke | Built-in values and sender override range checked in host |
| Candidate ordering and score threshold | ✅ | | | Host smoke test exists |
| Relaxed candidate matching and score degradation | ⬜ | | ⬜ studies | RAW slightly beyond tolerance should remain a candidate and score lower than ideal timing |
| Encode rejection / invalid inputs | ✅ | | | Undersized buffer, unknown id, bad length |
| RAW-only mode (0 candidates) | ✅ | | | Host smoke test exists |
| Tolerance boundaries | ✅ | | | SPACE_ENC ±25% boundaries checked in host smoke |
| Verified/generated fixture schema | ✅ | | | YAML checks and generated candidate formula checks added |
| Examples build | | ✅ | | Build tests exist |
| RMT TX RAW send | | ✅ sketch build | ✅ NEC smoke | TX peer sketch + two-board smoke |
| RMT RX RAW receive | | ✅ sketch build | ✅ NEC smoke | RX dut sketch + two-board smoke |
| TX->RX loop | | | ✅ NEC smoke | Two-board pytest exists |
| Protocol matrix | | ✅ sketch build | ✅ | ESP32IRPulseKit TX -> ESP32IRPulseKit RX checks NEC/SONY12/SAMSUNG32/JVC |
| A/C protocol matrix | ✅ codec_smoke | ✅ sketch build | ✅ | ESP32IRPulseKit TX -> ESP32IRPulseKit RX self round-trip, one state per vendor (Panasonic/Gree/Mitsubishi/Fujitsu/Daikin/Toshiba/Samsung/Sharp) |

## Hardware Setup

The standard hardware test setup uses two boards.

- TX board: sends known `protocol + bits` commands or `raw_ticks`
- RX board: receives with `IRReceiver` and prints decoded results to Serial
- pytest: controls both serial ports and asserts expected protocol/bits/score
- GPIO/inversion settings: injected through `build_config.toml` from `.env` keys `TEST_IR_TX_GPIO`, `TEST_IR_RX_GPIO`, `TEST_IR_TX_INVERTED`, and `TEST_IR_RX_INVERTED`

`hardware/link_smoke/` is the stable release-gate smoke test. It checks representative paths quickly and is part of normal release verification.

`hardware/protocol_matrix/` is the multi-protocol ESP32IRPulseKit TX -> ESP32IRPulseKit RX hardware check. It is broader than `link_smoke` and is part of normal release verification.

`hardware/protocol_matrix_ac/` is the A/C analog: an ESP32IRPulseKit TX -> ESP32IRPulseKit RX self round-trip for the `ac::` layer, one representative state per vendor, decoded via `ac::decodeAny`. It gates the A/C layer on hardware without any external library (cross-implementation interop stays in `studies/compat_matrix_ac/`). Part of normal release verification.

`studies/compat_matrix/` is optional compatibility and investigation coverage. Each test directory keeps the primary sketch as RX and `peer_tx/` as TX. Keeping the peer name fixed as `tx` lets external-library variants reuse `TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3`. The matrix records score, raw_len, and decode results as observations so physical setup and external-library timer variation can be evaluated.

The standard automated hardware target is a **two-board ESP32-S3 setup** for now. ESP32 classic, ESP32-C3/C6, and other SoCs are checked first with `examples/`. If a SoC-specific difference or bug appears, it can be promoted to an optional profile.

Single-board loopback is auxiliary. Direct GPIO loopback can change the inversion conditions compared with a real IR receiver module, so it is not the primary pass/fail baseline.

The peer board does not initially use a separate reference IR library. Baselines are kept in this order:

1. `protocol + bits` generated from protocol specs
2. Hand-written or reviewed fixed `raw_ticks`
3. Captured real-world fixtures promoted after review
4. External library comparisons only when compatibility testing is needed

## Signal Data

IR signal data lives under `tests/pc/fixtures/`.

- `generated/`: ideal waveforms generated from protocol specs and BITS
- `verified/`: hand-written or reviewed fixed RAW data
- `captured/`: unreviewed RAW captured from real remotes or hardware tests

Hardware tests distinguish two send modes.

- `SEND protocol bits`: tests the integrated `IRSender` and `IRReceiver` path
- `SEND_RAW raw_ticks`: tests decode behavior against known waveforms

## Decode Score Fixture Policy

Decode tests cover candidate formation and score ranking, not only strict pass/fail.

- Host tests keep fixed RAW inputs for ideal timing, tolerance boundaries, slightly out-of-tolerance but classifiable timing, and clearly broken timing
- Slightly out-of-tolerance waveforms should remain decode candidates and score lower than ideal timing for the same protocol
- Clearly broken waveforms should produce no candidate, or be dropped by score threshold
- For SPACE_ENC, classifiable jitter stays sufficiently far from the 0/1 space midpoint; ambiguous midpoint cases are rejection cases
- For BIPHASE, jitter that preserves the half-bit/grid structure is a candidate case; broken grid structure is a rejection case
- `studies/compat_matrix` observes timing variation from external libraries and physical setup; reproducible cases should be promoted into `pc/fixtures`

## Priority

1. Select the Arduino host runner and add host runtime tests.
2. Add build tests for examples and minimal sketches.
3. Add two-board TX/RX hardware tests.
4. Add host tests for noisy RAW fixtures that verify candidate retention and score degradation.
5. Promote captured real remote RAW fixtures into `pc` and `hardware` tests.
