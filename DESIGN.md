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
- The carrier is phase-aligned and symbol-encoded by default; `setPhaseAlignedCarrier(false)` selects the free-running hardware carrier instead — see section 12

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

- pc: PC-only automatic tests — `fixtures` (signal data + checks), `codec_smoke` (Arduino host run), `compile` (ESP32 build-only)
- hardware: two-board RMT TX/RX pass/fail regression
- studies: on-demand board investigations that record observation logs (not auto-collected)

## 12. Carrier generation and timing model

Both TX and RX use the ESP32 **RMT** peripheral: hardware-deterministic envelope
(mark/space) timing, non-blocking TX, and hardware-timestamped RX with no
interrupt-latency jitter on the captured edges. RAW ticks are 10 µs.

### Phase-aligned carrier (default)

The TX carrier is **phase-aligned and symbol-encoded**. The TX channel runs at
1 µs resolution and each mark is emitted as an integer number of full carrier
cycles starting at phase 0. Every mark therefore holds a deterministic cycle
count, and the demodulated mark — and the space after it — are stable frame to
frame. All protocols transmit at spec-standard timing (e.g. JVC zero-space
525 µs).

Trade-offs:

- Mark width is quantized to whole carrier cycles (~26 µs at 38 kHz), well inside
  standard IR tolerance (±25–30 %).
- The carrier period itself is quantized to whole microseconds (`period =
  round(1e6 / carrier_hz)` at the 1 µs channel resolution), so the emitted
  frequency is rounded: 38 kHz → 26 µs ≈ 38.46 kHz (+1.2 %), 36 kHz → 28 µs ≈
  35.71 kHz (−0.8 %), 40 kHz → 25 µs (exact). There is no per-cycle dithering to
  recover the fractional part; the residual error is well within IR receiver
  passbands, so it is left as-is.
- A frame expands to roughly one symbol per carrier cycle (hundreds to ~1000+ per
  frame). The RMT driver streams it from the channel memory; refill headroom is
  set by `IRSender::setTxMemBlocks(n)` (1 block = `SOC_RMT_MEM_WORDS_PER_CHANNEL`
  symbols). The default is one block, which transmits cleanly under normal
  interrupt load; raise it for applications with heavy ISR contention (e.g.
  concurrent flash writes). Long frames use a larger transient buffer.

### Hardware-carrier fallback

`IRSender::setPhaseAlignedCarrier(false)` selects the free-running hardware
carrier (`rmt_apply_carrier`) instead: far fewer symbols, but the carrier phase is
not reset per mark, so the demodulated mark wobbles by ±1 carrier cycle (~26 µs).
That wobble can push short-mark protocols (e.g. JVC, AEHA) outside the tightest
external-decoder windows (IRremoteESP8266), so the phase-aligned default is
preferred for cross-library interop. Evidence:
`tests/studies/{phase_aligned_carrier,carrier_loopback,jvc_timing_sweep}`.

For AC the right carrier depends on the vendor's timing margins. The
phase-aligned carrier is the safe default; the hardware carrier is a memory
optimization that only some vendors tolerate. At roughly one symbol per carrier
cycle a multi-frame AC burst expands to several thousand symbols (~17 KB),
allocated transiently and streamed through the channel buffer, with a higher
refill-underrun risk under heavy interrupt load — that is the cost of
phase-aligned. The hardware carrier avoids it but each mark edge wobbles by ±1
carrier cycle (~26 µs).

Measured (`tests/studies/compat_matrix_ac/.../study_carrier_ab.py`):

- Panasonic: phase-aligned and hardware both 150/150 — its loose timing
  tolerates the wobble, so the hardware carrier is fine and cheaper.
- Gree: phase-aligned 50/50 vs hardware ~55%. Its zero-space (540 µs) is shorter
  than its bit mark (620 µs), so the wobble pushes spaces out of the receiver's
  tolerance and ~half the frames are rejected — the hardware carrier is not
  usable here.

So phase-aligned is the cross-vendor-safe choice; the hardware carrier is opt-in
per vendor (e.g. Panasonic). Neither affects byte integrity — frames are
checksum-validated; the carrier affects delivery rate. The phase-aligned encoder
is never size-limited: durations beyond the 15-bit field are split across
symbols, so even the inter-block gap (well under the ~33 ms single-symbol limit
at 1 µs) is never the constraint; the cost is mark expansion (symbol count).
