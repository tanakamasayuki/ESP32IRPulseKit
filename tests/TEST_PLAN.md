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
| GitHub Actions | host, build |
| As needed | manual |

## Initial Coverage

| Feature | host | build | hardware | manual | Status |
| --- | --- | --- | --- | --- | --- |
| NEC encode/decode roundtrip | ✅ | ✅ | ⬜ | | Host and build smoke tests exist |
| NEC repeat decode | ✅ | | ⬜ | | Host smoke test exists |
| SONY encode/decode roundtrip | ⬜ | | ⬜ | | Planned |
| Samsung/JVC encode/decode roundtrip | ⬜ | | ⬜ | | Planned |
| AEHA variable-length decode | ⬜ | | ⬜ | | After encode semantics are clarified |
| Candidate ordering and score threshold | ✅ | | | | Host smoke test exists |
| Tolerance boundaries | ⬜ | | | | Planned |
| Verified fixture schema | ✅ | | | | YAML checks added |
| Examples build | | ⬜ | | | Planned in build tests |
| RC5/RC6 decode | ⬜ | | ⬜ | | Supported scope needs review |
| RMT TX RAW send | | | ⬜ | | Planned with two boards |
| RMT RX RAW receive | | | ⬜ | | Planned with two boards |
| TX->RX loop | | | ⬜ | | Planned with TX/RX ESP32 boards |
| Real remote receive | | | | ⬜ | Candidate for fixture promotion |

## Hardware Setup

The standard hardware test setup uses two boards.

- TX board: sends known `protocol + bits` commands or `raw_ticks`
- RX board: receives with `IRReceiver` and prints decoded results to Serial
- pytest: controls both serial ports and asserts expected protocol/bits/score

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
