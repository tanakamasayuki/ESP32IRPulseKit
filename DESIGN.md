# ESP32IRPulseKit Design Notes

> Japanese: [DESIGN.ja.md](DESIGN.ja.md)

This document is for implementation notes. The public API contract lives in [SPEC.md](SPEC.md).

## 1. Design Direction

- Receive API: `esp32irpk::IRReceiver`
- Send API: `esp32irpk::IRSender`
- RMT-dependent behavior is contained under `src/hal/`
- RAW/BITS conversion lives under `src/codec/`
- Protocol specs and Frame types live under `src/protocols/`
- Frame types only convert between logical fields and `IRDecodedBits`; they do not implement decode/encode

## 2. Data Flow

Receive:

```text
RMT RX -> RAW ticks -> decode -> IRDecodedBits -> Frame::fromBits()
```

Send:

```text
Frame::toBits() -> IRDecodedBits -> encode -> RAW ticks -> RMT TX
```

RAW uses `1 tick = 10us`. Protocol specs use microseconds.

## 3. idle threshold And gap threshold

`idle_threshold_us` is the RMT no-signal threshold. `gap_threshold_us` is the codec threshold used to split concatenated frames in RAW data.

They are independent.

- A long RMT idle threshold may return multiple frames as one RAW buffer
- The codec treats spaces at least `gap_threshold_us` long as gaps for each protocol
- Gaps are used for splitting but are not penalized in score

## 4. Receive Queue And consumed_len

The HAL converts RMT symbols to tick arrays and stores them in an internal queue.

`IRReceiver::read()` advances processed RAW length using the best candidate's `consumed_len`. If one RMT receive contains multiple frames, the next `read()` processes the remaining waveform.

When the queue is full, older entries are dropped and `queue_overflow_count` is incremented.

## 5. Decode Candidates And score

Decode runs against all registered protocols.

High-level flow:

1. Reject obvious mismatches such as header, bit length, and broken mark/space layout
2. Keep waveforms as candidates when the protocol encoding rules can still classify the bits
3. Accumulate timing error from nominal values into `score`
4. Sort candidates by descending score; ties use registration order
5. Drop candidates below `setScoreThreshold()`

`IRProtocolSpec::bit_tol_pct` is the reference range for a good match, not the absolute candidate limit. Real IR receiver modules can shift marks/spaces systematically, so slightly out-of-spec signals should remain candidates when their bits are still clear, with the degradation reflected in score.

Candidate-formation principles:

- Reject waveforms that clearly cannot be valid frames
- Treat header/repeat/header-like structure more conservatively than body bits because it strongly identifies the protocol
- For SPACE_ENC, classify bits by nearest expected space when 0/1 spaces are sufficiently separated
- For BIPHASE, keep candidates while the half-bit/grid structure is still valid
- Reject ambiguous bit-classification regions, broken mark/space order, and bit counts outside the protocol range

It is expected that similar protocols can remain as candidates for the same RAW input. Final ranking is decided by score gap, protocol-specific adjustments, and registration order. Score internals are not public API, but implementation should preserve the policy of "classify when possible and score the error" rather than "reject immediately when a strict window is exceeded."

`score` is a relative public API value. Its absolute meaning is not fixed.

Score internals are not exposed in the public API. Normal users should inspect `score` and `decoded`. If detailed diagnostics become necessary, they should be emitted through future `ESP_LOGD` / `ESP_LOGV` decode traces.

## 6. Protocol ID Policy

`IRProtocolID` is split by waveform timing and by whether `IRDecodedBits.bits` has compatible logical interpretation.

Examples:

- Samsung 32-bit and 36-bit use different IDs
- Panasonic 40-bit and 48-bit use different IDs
- AEHA can use one ID when the same Frame interpretation can handle variable length

## 7. repeat_count

`repeat_count` is the extra send count.

- `0`: send once
- `2`: initial send + 2 extra sends = 3 sends total
- `-1`: use `IRProtocolSpec::default_repeat_count`

Some Sony devices require 3 total frames, so Sony defaults use `default_repeat_count = 2`.

## 8. TX Carrier

RMT TX enables carrier modulation so common IR receiver modules can receive the signal.

- RAW/BITS tick arrays represent the mark/space envelope
- The HAL overlays the carrier during mark periods before driving GPIO
- The library default is 38kHz
- `IRProtocolSpec::carrier_hz` is the protocol preferred value. `0` uses the default
- Built-in protocols keep `0` when their preferred carrier has not been verified; 38kHz is not stamped on every protocol
- Current built-in preferred values are NEC/AEHA/Panasonic/Samsung=38kHz, JVC=37.9kHz, Sony=40kHz, and RC5/RC6=36kHz
- `IRSender::setCarrierHz()` is a sender-level explicit override and takes precedence over protocol preferences
- `clearCarrierHz()` removes the explicit override
- Duty cycle is not public API; the implementation uses an internal fixed duty of about 1/3
- Carrier changes after begin apply from the next send. Changes while sending are rejected
- Hardware TX/RX smoke tests must catch missing carrier configuration

## 9. Logging Policy

On ESP32 hardware, use ESP-IDF `ESP_LOGx`.

- `E`: unrecoverable failures such as begin failure or RMT channel allocation failure
- `W`: recoverable issues such as truncate, overflow, or capacity shortage
- `I`: major state changes such as begin/end, default protocol registration, and idle threshold selection
- `D`: diagnostic data such as decode decisions, score, and send requests
- `V`: high-volume data such as RAW ticks and RMT symbols

`D` is the practical upper level for normal diagnostics. `V` is for issue investigation.

## 10. Comment Policy

- Library implementation comments are English
- Examples and test READMEs can be split into Japanese and English files
- Sample code avoids `using namespace` and writes `esp32irpk::` explicitly

## 11. Test Strategy

The full test strategy is in [tests/TEST_PLAN.md](tests/TEST_PLAN.md).

- host: Arduino host runtime assertions for codec, Frame, and fixtures
- build: compile examples and minimal sketches for ESP32
- hardware: verify RMT TX/RX paths with two ESP32 boards
- manual: real remotes, distance, ambient light, and other checks that need human observation
