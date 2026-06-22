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
- AEHA uses one ID for the whole家製協 family (variable length), including Kaseikyo/Panasonic frames

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
- Current built-in preferred values are NEC/AEHA/Samsung=38kHz, JVC=37.9kHz, Sony=40kHz, and RC5/RC6=36kHz
- `IRSender::setCarrierHz()` is a sender-level explicit override and takes precedence over protocol preferences
- `clearCarrierHz()` removes the explicit override
- Duty cycle is not public API; the implementation uses an internal fixed duty of about 1/3
- Carrier changes after begin apply from the next send. Changes while sending are rejected
- Hardware TX/RX smoke tests must catch missing carrier configuration
- The carrier is generated by the RMT hardware and its phase is not controllable per mark — see section 12 for the consequence (and the one JVC timing exception)

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

## 12. Timing model: RMT vs timer, and the carrier-phase limit

This library drives both TX and RX with the ESP32 **RMT** peripheral. That choice
shapes its timing characteristics — strong on envelope timing and CPU cost, with
one narrow weakness around carrier phase. This section records the trade-off and
the single protocol exception it forces.

### What RMT gives us

- **Deterministic envelope (mark/space) timing.** Durations come from hardware,
  not a software loop, so frame-to-frame jitter is small and TX does not block the
  CPU. RX is hardware-timestamped (no interrupt-latency jitter on the captured
  edges).
- **Internal 10 µs tick.** The driver runs at 100 kHz resolution, so every
  emitted/decoded duration is quantized to 10 µs. This is well inside every
  standard IR tolerance (±25–30 %, i.e. tens to hundreds of µs), so it is a
  non-issue for decoding — but it does mean a spec value like JVC's 525 µs is
  emitted as 530 µs (53 ticks).

### The carrier-phase limitation

The 38 kHz carrier is produced by the RMT hardware (`rmt_apply_carrier`) as a
**free-running oscillator gated onto the marks**. There is no API to reset the
carrier phase at each mark boundary. Consequently the number of whole carrier
cycles captured inside a mark can wobble by **±1 (~26 µs at 38 kHz)** from mark to
mark, because the mark edge lands at a random carrier phase. This is a transmit
characteristic that surfaces at the receiver: the demodulator (TSOP) counts
cycles, so the demodulated mark — and therefore the following space — shifts by up
to one carrier period.

- For **most protocols this is harmless**: their receiver windows have plenty of
  margin for a ±26 µs wobble.
- The **one exception is JVC**, whose zero-space window in the strictest external
  decoder (IRremoteESP8266: `(526−50)×1.25 ≈ 594 µs`) is the tightest of all, and
  whose 525→530 µs mark sits at a half-integer carrier-cycle point (worst-case
  wobble). So PulseKit deliberately emits a **non-standard 480 µs JVC zero-space**
  (vs the 525 µs spec) to push the received space clear of that window. This is a
  transmit-margin workaround, documented in `src/protocols/JVC.h`; all other
  protocols use spec-standard timing. The proper fix would be a phase-aligned,
  symbol-encoded carrier at ~1 µs resolution — a much larger change, deferred.
  Evidence: `tests/hardware/{carrier_loopback,jvc_timing_sweep,jvc_verify_arduino}`.

### Why timer-based libraries do not need the JVC tweak

Libraries such as Arduino-IRremote and IRremoteESP8266 generate the carrier **in
software** — a busy-loop (or a PWM) that is (re)started at the beginning of each
mark. Because the carrier restarts with the mark, its phase is aligned and each
mark holds an integer, deterministic number of cycles — so they have **no
carrier-phase jitter and need no JVC timing tweak**. (Our self-test confirms this:
IRremoteESP8266 → IRremoteESP8266 JVC decodes 5/5.) The price is paid elsewhere:
the software carrier **busy-loops and blocks the CPU** for the whole frame, and
their RX uses **timer/edge interrupts**, so the captured envelope timing carries
interrupt latency and tick quantization and can be disturbed by other interrupts.

### RMT vs timer — strengths and weaknesses

| Aspect | RMT (this library) | Timer / software carrier (others) |
|---|---|---|
| Envelope timing | Hardware-deterministic, low jitter | ISR latency / busy-loop scheduling jitter |
| CPU during TX | Free (hardware-driven, non-blocking) | Busy-loop blocks the CPU for the frame |
| RX capture | Hardware timestamps, no ISR jitter | Timer/edge ISR → latency + tick quantization |
| Time resolution | Fine clock, library uses a 10 µs tick | Often coarser, but the carrier is phase-aligned |
| Carrier phase | Free-running, not resettable → ±1-cycle jitter | Restarted per mark → phase-aligned, no jitter |
| JVC | Needs the 480 µs zero-space workaround | Works at spec timing, no tweak |
| Multi-channel | Several independent RMT channels | Limited (timer / CPU-bound) |

**Bottom line.** RMT is the better fit for a modern ESP32 library — deterministic,
non-blocking, multi-channel, low-jitter envelopes. Its only real weakness is the
uncontrollable carrier phase, and that bites exactly one protocol/receiver
combination (JVC on the tightest external window), handled by one documented
timing tweak. Timer/software designs win only on that carrier-phase point, at the
cost of CPU occupancy and noisier envelope timing.
