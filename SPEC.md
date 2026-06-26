# ESP32IRPulseKit External Specification

> Japanese: [SPEC.ja.md](SPEC.ja.md)

ESP32IRPulseKit is an IR remote send/receive library for ESP32 Arduino Core 3.x / ESP-IDF 5.x using the new RMT driver.

This document defines the public API contract. Implementation notes, scoring policy, and logging policy are in [DESIGN.md](DESIGN.md).

## 1. Scope

- Target environment: ESP32 Arduino Core 3.x or later
- Target language: the standard C++ environment used by Arduino Core 3.x
- Namespace: `esp32irpk`
- Time units:
  - Protocol specs use microseconds, `*_us`
  - RAW arrays use ticks, `1 tick = 10us`

## 2. Core Types

### 2.1 RAW

RAW is a tick array of alternating mark/space durations. It starts with a mark.

```cpp
namespace esp32irpk {

struct IRRawTickView {
  const uint16_t* ticks = nullptr;
  size_t len = 0;
};

struct IRRawTickBuffer {
  uint16_t* ticks = nullptr;
  size_t capacity = 0;
  size_t len = 0;
};

}
```

`IRRawTickView` may refer to an internal buffer. A view returned by `IRReceiver::read()` is valid until the next `read()` call or `end()`.

### 2.2 BITS

`IRDecodedBits` is the normalized bit representation after protocol detection.

```cpp
namespace esp32irpk {

enum class IRFrameType : uint8_t {
  NORMAL = 0,
  REPEAT = 1,
};

struct IRDecodedBits {
  IRProtocolID protocol_id = IRProtocolID::UNKNOWN;
  IRFrameType frame_type = IRFrameType::NORMAL;
  uint16_t bit_length = 0;
  uint64_t bits = 0;

  bool isRepeat() const;
};

}
```

- `bit_length` is `0..64`.
- Normal frames use `frame_type == NORMAL`.
- Repeat frames use `frame_type == REPEAT`. The default repeat representation is `bit_length == 0` and `bits == 0xffffffffffffffff`.
- Bit order follows `IRProtocolSpec::lsb_first`, which maps the on-air bit order
  onto `bits`:
  - `true`: the first transmitted bit is `bits` bit 0 (LSB).
  - `false`: the first transmitted bit is the most significant bit of
    `bit_length` (`bits` bit `bit_length - 1`).
  This mirrors each protocol's on-air transmission order — NEC, Sony, JVC, Samsung
  (SAMSUNG32), and AEHA (including Kaseikyo/Panasonic) send LSB-first; RC5/RC6 and
  SAMSUNG36 send MSB-first. The encoder and decoder honor the flag symmetrically, so an
  in-library TX → RX round-trip always recovers the same `bits`.
  - Other libraries may index the *same* on-air signal from the opposite end
    (for example IRremoteESP8266 stores the NEC first-sent bit at the MSB of its
    integer), so their value can appear bit-reversed even though the waveform is
    identical. This is a representation difference, not an incompatibility; the
    `compat_matrix` tests record it as `bit_order = reversed`.

### 2.3 Protocol Spec

```cpp
namespace esp32irpk {

struct IRPulseUs {
  uint32_t mark_us = 0;
  uint32_t space_us = 0;
};

enum class IRProtocolScheme : uint8_t {
  UNKNOWN = 0,
  SPACE_ENC = 1,
  BIPHASE = 2,
};

enum class IRProtocolFamily : uint8_t {
  UNKNOWN = 0,
  NEC_LIKE = 1,
  AEHA = 2,
  SONY = 3,
  RC5 = 4,
  RC6 = 5,
};

struct IRProtocolSpec {
  IRProtocolID protocol_id = IRProtocolID::UNKNOWN;
  char name[16] = {};
  IRProtocolScheme scheme = IRProtocolScheme::UNKNOWN;
  IRProtocolFamily family = IRProtocolFamily::UNKNOWN;

  IRPulseUs header{};
  IRPulseUs one{};
  IRPulseUs zero{};
  IRPulseUs trailer{};

  uint32_t gap_threshold_us = 0;
  uint32_t idle_threshold_us = 0;
  uint32_t carrier_hz = 0;

  bool lsb_first = true;

  uint16_t bit_length = 0;
  uint16_t min_bit_length = 0;
  uint16_t max_bit_length = 0;

  bool has_repeat = false;
  IRPulseUs repeat_header{};
  uint32_t repeat_gap_us = 0;
  int8_t default_repeat_count = 0;

  uint16_t bit_tol_pct = 25;
  uint8_t order = 0;
};

}
```

- `gap_threshold_us`: minimum gap used to split frames while decoding. `0` disables gap splitting.
- `idle_threshold_us`: preferred RMT idle threshold. `0` uses the receiver setting.
- `carrier_hz`: preferred carrier frequency for sending. `0` uses the library default.
- Built-in protocols keep `0` when their preferred carrier has not been verified. The library does not stamp every protocol with 38kHz.
- `name`: fixed-size copied display name, maximum 15 characters plus the terminating NUL. `addProtocol()` copies `IRProtocolSpec` by value, so the name does not depend on external string lifetime.
- Fixed-length protocols use `bit_length`.
- Variable-length protocols use `min_bit_length..max_bit_length`. A `0` bound falls back to `bit_length`.
- `default_repeat_count` is the default extra send count used when `repeat_count < 0`. `0` means one send; `2` means three sends total.
- `bit_tol_pct` is the standard error range for a good signal for that protocol. It is not an absolute candidate rejection limit. A mark/space outside this range may still be kept as a decode candidate when the frame shape and bit classification are still clear; the extra error is reflected in `score`.
- `order` is assigned by registration. Users do not need to set it.

### 2.4 Decode Result

```cpp
namespace esp32irpk {

inline constexpr size_t kDefaultMaxDecodeCandidates = 4;

enum class IRResultFlags : uint8_t {
  NONE = 0,
  DECODE_SKIPPED = 1 << 0,
  RAW_TRUNCATED = 1 << 1,
  RMT_OVERFLOW = 1 << 2,
};

struct IRDecodeCandidate {
  IRProtocolID protocol_id = IRProtocolID::UNKNOWN;
  char name[16] = {};
  uint8_t order = 0;
  int16_t score = 0;
  size_t consumed_len = 0;
  IRDecodedBits decoded{};
};

template <size_t MaxCandidates = esp32irpk::kDefaultMaxDecodeCandidates>
struct IRReceiveResult {
  IRRawTickView raw{};
  IRResultFlags flags = IRResultFlags::NONE;
  uint8_t count = 0;
  IRDecodeCandidate candidates[MaxCandidates]{};

  const IRDecodeCandidate* candidate() const;
  const IRDecodedBits* bits() const;

  bool hasFlag(IRResultFlags bit) const;
  bool decodeSkipped() const; // hasFlag(DECODE_SKIPPED)
  bool truncated() const;     // hasFlag(RAW_TRUNCATED)
  bool rmtOverflow() const;   // hasFlag(RMT_OVERFLOW)
};

struct IRRxStats {
  uint32_t queue_overflow_count = 0;
  uint32_t rmt_overflow_count = 0;
  uint32_t raw_truncated_count = 0;
};

}
```

- `candidates` are sorted by descending score. Ties are broken by earlier registration order.
- `candidate()` returns the best candidate. `bits()` returns the best candidate's BITS. Both return `nullptr` when there is no candidate.
- `hasFlag(bit)` tests a result flag; `decodeSkipped()`, `truncated()`, and `rmtOverflow()` are named shortcuts for the three flags.
- `score` is a relative measure of how closely the received waveform matches the protocol spec. A waveform similar to multiple protocols may produce multiple candidates; users normally inspect the best candidate and the score gap. The absolute score value and formula are not compatibility guarantees.
- `DECODE_SKIPPED`: decode was skipped, for example in RAW-only mode.
- `RAW_TRUNCATED`: RAW exceeded the internal limit and was truncated.
- `RMT_OVERFLOW`: RMT receive may have overflowed.

### 2.5 Decode Matching And score

Decode is not an exact-match-only operation. Real IR receive timings can shift systematically with the receiver module, distance, angle, carrier duty, and ambient light, so ESP32IRPulseKit separates candidate formation from scoring.

- Obvious mismatches are rejected early, for example a very different header, broken mark/space layout, bit count outside the protocol range, invalid repeat shape, or a missing required gap/trailer.
- Waveforms inside `bit_tol_pct` are treated as high-quality matches.
- Waveforms slightly outside `bit_tol_pct` can still remain candidates when the protocol encoding rules can classify the bits clearly.
- For SPACE_ENC protocols whose 0/1 spaces are well separated, decoding may classify each bit by the nearest expected space instead of relying only on strict tolerance windows. The error from the nominal timing is accumulated into `score`.
- For BIPHASE protocols, candidates may remain while the half-bit/grid structure is still valid; deviation from the unit timing is reflected in `score`.
- It is normal for similar protocols to remain as candidates for the same RAW input. Final priority is descending score, then registration order for ties.

Therefore, a protocol timing tolerance such as NEC bit timing ±25% is the reference for a good match, not the widest possible candidate range. The wider internal candidate tolerance is an implementation detail, but waveforms whose bit classification is ambiguous or whose frame shape is invalid are rejected.

## 3. Built-in Protocols

Built-in protocol specs are available under `esp32irpk::specs`.

```cpp
esp32irpk::specs::NEC
esp32irpk::specs::AEHA
esp32irpk::specs::SONY12
esp32irpk::specs::SONY15
esp32irpk::specs::SONY20
esp32irpk::specs::SAMSUNG32
esp32irpk::specs::SAMSUNG36
esp32irpk::specs::JVC
esp32irpk::specs::RC5
esp32irpk::specs::RC6_M0_16
esp32irpk::specs::RC6_M6_32
```

`IRProtocolID` is separated not only by waveform timing but also by the logical interpretation of `IRDecodedBits.bits`. For example, Samsung 32-bit and 36-bit use different IDs.

### 3.1 Candidate Protocols (not yet implemented)

The current 11 cover the large majority of remote-control use, so the following
are **candidates**, not requirements. If the goal is only learn-and-replay (raw
capture/replay), the raw-tick path already handles any protocol and no addition
is needed; explicit support matters only when you need to decode into meaningful
bits or generate codes. They are triaged into three tiers by practical value.

**Tier A: effectively covered by existing decoders (only ID/label mapping)**

| Candidate | Family | Notes |
|---|---|---|
| NEC extended / NEC2 | NEC_LIKE | 16-bit address; raw bits already obtainable via NEC |
| Denon-/JVC-/Sharp-/Mitsubishi-Kaseikyo | AEHA | Only the vendor ID differs; the AEHA decoder returns correct bits |

→ Mostly adding an `IRProtocolID` and labeling the decode result. No new decode logic.

**Tier B: high value to add (real devices still exist; small/medium effort)**

| Candidate | Family | Carrier | Notes |
|---|---|---|---|
| Pioneer | NEC_LIKE | 40kHz | Sends NEC twice. AV receivers |
| Onkyo | NEC_LIKE | 38kHz | NEC-family parameter variant |
| Sharp | custom (space-enc) | 38kHz | AQUOS etc.; needs the inverted expansion/check frame |
| Denon | custom (space-enc) | 38kHz | Older Denon (Kaseikyo often suffices) |
| RC6A (variable length) | BIPHASE | 36kHz | MCE/Windows remotes, Xbox 360 IR, some STBs |

**Tier C: skip by default (implement only on a concrete need)**

- RCMM, RECS80, Nokia NRC17, Grundig, Nubert, XMP, F12, G.I.Cable, Whynter
- Lego Power Functions / MagiQuest (toys / venues)
- Bang & Olufsen: **455kHz carrier** — cannot be received by a normal 38kHz TSOP,
  hardware-wise a different beast. **Best left unsupported.**
- Air-conditioner / heat-pump protocols (Daikin / Mitsubishi-AC / Panasonic-AC /
  Gree / Coolix, …): a single button sends a whole multi-byte state, which does
  not fit the generic 64-bit single-frame decode/encode model. These are handled
  by a separate layer (`esp32irpk::ac`) instead of the generic decoder, so they
  are not generic candidates — see section 11 (Panasonic is implemented there).

Policy: if adding, cap at **Tier B**; revisit Tier C only when demand appears.
Tier A can be added cheaply purely to grow the supported-protocol count.

## 4. Protocol Registration And Initialization

`IRReceiver` and `IRSender` copy protocol specs internally.

The following settings are valid only before `begin()`. They return `false` after `begin()`.

- `setPin()`
- `setInverted()`
- `addProtocol()`
- `clearProtocols()`
- `setDecodeCandidates()`
- `setIdleThresholdUs()`
- `setScoreThreshold()`
- `setMaxRxSymbols()`

### 4.1 Protocol Selection

`addProtocol()` registers a protocol spec. It is valid only before `begin()`.

- The spec is copied by value.
- If a spec with the same `protocol_id` is already registered, `addProtocol()` replaces it; otherwise the spec is appended.
- To customize or override a built-in protocol, register a modified copy of an `esp32irpk::specs::*` spec. The built-in with the same `protocol_id` is replaced.

`clearProtocols()` removes all registered protocols.

### 4.2 Default Registration

Default registration at `begin()` differs between sender and receiver:

- `IRSender`: every built-in protocol whose `protocol_id` is not already registered is added (top-up). A sender therefore always has the full built-in set available; registering a spec customizes or overrides one entry, but does not restrict the set.
- `IRReceiver`: if the decode candidate count is at least 1 and no protocols are registered, all built-in protocols are registered. If at least one protocol is registered, only the registered protocols are used, which lets you restrict the decode candidate set.
- `IRReceiver`: if the decode candidate count is 0, RAW-only mode is used and no protocols are registered.

Specs added with `addProtocol()` keep their relative registration order; built-ins added by top-up at `begin()` are ordered after them. This order is the tie-breaker described in section 2.4.

### 4.3 Decode Candidate Count

`setDecodeCandidates(n)` sets how many decode candidates `IRReceiver` keeps per `read()`. It is valid only before `begin()`.

- Range is `0..MaxCandidates` (the `IRReceiver` template parameter). It returns `false` after `begin()`, or when `n > MaxCandidates`.
- The default is `MaxCandidates`.
- `n >= 1`: `read()` decodes and keeps up to `n` best-scoring candidates, sorted as described in section 2.4.
- `n == 0`: RAW-only mode. `read()` returns RAW with `DECODE_SKIPPED`, no decode runs, and no built-in protocols are registered at `begin()` (see section 4.2). RAW-only is selected only this way; it is not implied by leaving protocols unregistered.

### 4.4 Idle Threshold

The default `setIdleThresholdUs()` value is `30000us`. When decode is enabled, the receiver uses the larger of the receiver setting and the maximum non-zero `idle_threshold_us` among registered protocols.

### 4.5 RX Capture Capacity

`setMaxRxSymbols(symbols)` sets the maximum number of RMT symbols a single capture can hold. It is valid only before `begin()`.

- The default is sized for normal short remote frames. Captures that exceed the capacity are truncated and `RAW_TRUNCATED` is set.
- A long waveform such as an air-conditioner frame (section 11) needs a larger value. Raise it together with `setIdleThresholdUs()` so the whole burst lands in one capture.
- A larger capacity costs RAM per receiver, so it is opt-in rather than a global default.

## 5. IRReceiver

```cpp
namespace esp32irpk {

template <size_t MaxCandidates = esp32irpk::kDefaultMaxDecodeCandidates>
class IRReceiver {
public:
  explicit IRReceiver(int gpio);
  IRReceiver(int gpio, bool inverted);

  bool setPin(int gpio);
  bool setInverted(bool inverted);

  bool setDecodeCandidates(uint8_t n);
  bool setIdleThresholdUs(uint32_t us);
  bool setScoreThreshold(int16_t score);
  bool setMaxRxSymbols(size_t symbols);

  bool addProtocol(const IRProtocolSpec& spec);
  bool clearProtocols();

  bool begin();
  void end();

  bool read(IRReceiveResult<MaxCandidates>& out);
  bool decode(const IRRawTickView& raw, IRReceiveResult<MaxCandidates>& out) const;

  IRRxStats stats() const;
  void resetStats();
};

}
```

### 5.1 read

- `true`: received RAW is available. `out.raw.len > 0`.
- `false`: no RAW is available. `out` is undefined.

When the decode candidate count is 0, `read()` returns RAW only and sets `DECODE_SKIPPED`.

When the decode candidate count is at least 1, `read()` returns RAW and decode candidates. If no candidate matches, it still returns `true` when RAW was received, with `out.count == 0`.

### 5.2 decode

`decode(raw, out)` decodes RAW using registered protocols. It does not receive from RMT.

- `true`: at least one decode candidate exists.
- `false`: no decode candidates exist.

## 6. IRSender

```cpp
namespace esp32irpk {

class IRSender {
public:
  explicit IRSender(int gpio);
  IRSender(int gpio, bool inverted);

  bool setPin(int gpio);
  bool setInverted(bool inverted);
  bool setCarrierHz(uint32_t hz);
  bool clearCarrierHz();
  bool disableCarrier();
  bool setCarrierDuty(float duty);
  bool setPhaseAlignedCarrier(bool enable);
  bool setTxMemBlocks(uint8_t blocks);

  bool addProtocol(const IRProtocolSpec& spec);
  bool clearProtocols();

  bool begin();
  void end();

  bool send(const IRRawTickView& raw, int8_t repeat_count = -1);
  bool send(const IRRawTickView* raw, int8_t repeat_count = -1);
  bool send(const IRDecodedBits& decoded, int8_t repeat_count = -1);
  bool send(const IRDecodedBits* decoded, int8_t repeat_count = -1);

  bool encode(const IRDecodedBits& decoded, IRRawTickBuffer& out_raw);
};

}
```

### 6.1 RAW Send

`send(raw)` sends a RAW tick array as-is.

- Returns `false` before `begin()`.
- Returns `false` when `raw.ticks == nullptr` or `raw.len == 0`.
- Pointer overloads return `false` for `nullptr`.
- For RAW send, `repeat_count < 0` is treated as `0`.
- Carrier uses the explicit `setCarrierHz()` override when set, otherwise the library default `38000`.

### 6.2 BITS Send

`send(decoded)` encodes `IRDecodedBits` to RAW and sends it.

- Returns `false` before `begin()`.
- Returns `false` when no registered protocol matches `decoded.protocol_id`.
- Fixed-length protocols require `decoded.bit_length == spec.bit_length`.
- Variable-length protocols require `decoded.bit_length` within `min_bit_length..max_bit_length`.
- Repeat frames can only be sent when `spec.has_repeat == true`.
- `repeat_count < 0` uses `spec.default_repeat_count`.
- `repeat_count >= 0` uses the provided value.
- Carrier is resolved in this order: explicit `setCarrierHz()` override, `spec.carrier_hz`, then the library default `38000`.
- Built-in preferred values: NEC/AEHA/Samsung use 38kHz, JVC uses 37.9kHz, Sony SIRC uses 40kHz, and RC5/RC6 use 36kHz.

### 6.3 Carrier Configuration

`setCarrierHz(hz)` explicitly fixes the sender carrier frequency.

- `hz == 0` is equivalent to `clearCarrierHz()`.
- Allowed range is `20000..60000` Hz. Values outside that range return `false`.
- Can be called before or after `begin()`.
- Changes after `begin()` apply from the next send. When possible, the RMT TX channel is updated immediately.
- Returns `false` when called while sending.

`clearCarrierHz()` removes the sender-level override.

- RAW send returns to the library default `38000`.
- BITS send uses protocol `carrier_hz` again.

`disableCarrier()` sends solid marks with no carrier modulation. Can be called before or after `begin()`; returns `false` while sending.

`setCarrierDuty(duty)` sets the carrier on-time fraction. The default is about `0.33`.

- Allowed range is `0 < duty < 1`. Values outside that range return `false`.
- Can be called before or after `begin()`; returns `false` while sending.
- The practical range is about `0.2`–`0.5`. A higher duty can extend range but draws more power, and at close range too high a duty can saturate the receiver and reduce reliability; a lower duty trades range for power saving. The optimum depends on distance, so `0.33` is the common recommendation.

### 6.5 Carrier Generation And TX Channel

`setPhaseAlignedCarrier(enable)` selects the carrier generation method. It fixes the TX channel layout, so it is valid only before `begin()` and returns `false` afterward.

- `true` (default): phase-aligned, symbol-encoded carrier. Each mark is emitted as an integer number of full carrier cycles starting at phase 0, so the demodulated mark is stable frame to frame. This is preferred for cross-library interoperability. The carrier period is quantized to whole microseconds (the channel runs at 1 µs resolution), so the emitted frequency is rounded to the nearest `1/N µs`: 38 kHz → 26 µs ≈ 38.46 kHz (+1.2 %), 36 kHz → 28 µs ≈ 35.71 kHz (−0.8 %), 40 kHz → 25 µs (exact). The error is well within IR receiver passbands; there is no per-cycle dithering.
- `false`: free-running hardware carrier (`rmt_apply_carrier`). Far fewer RMT symbols, but the carrier phase is not reset per mark, so the demodulated mark wobbles by ±1 carrier cycle.

`setTxMemBlocks(blocks)` sets the number of RMT memory blocks for the TX channel (1 block = `SOC_RMT_MEM_WORDS_PER_CHANNEL` symbols). It is valid only before `begin()`.

- `0` uses the library default (1 block).
- While sending, the RMT driver refills the channel from an interrupt. If another long-running interrupt blocks that refill, the channel underruns and the waveform is corrupted. More blocks lengthen the refill interval and raise tolerance to interrupt latency, which matters most on single-core ESP32-C parts with the radio active.
- If underruns occur, increase the block count or fall back to the hardware carrier with `setPhaseAlignedCarrier(false)` (lower TX precision, far fewer symbols).
- The RMT TX memory pool is shared with other users (e.g. addressable RGB LEDs) and each SoC has a limited number of blocks, so allocating all of them to one channel is not recommended.

The full carrier and timing model is in [DESIGN.md](DESIGN.md) §8 and §12.

### 6.6 encode

`encode(decoded, out_raw)` converts BITS to RAW without sending.

- The caller provides `out_raw.ticks` and `out_raw.capacity`.
- On success, `out_raw.len` is set to the generated tick count.
- On failure, returns `false`.

## 7. Frame Types

Protocol-specific Frame types live under `esp32irpk::frames`. They only convert between protocol-specific logical fields and `IRDecodedBits`.

Typical shape:

```cpp
namespace esp32irpk::frames {

struct NECFrame {
  uint16_t address = 0;
  uint8_t command = 0;
  bool is_repeat = false;

  static NECFrame fromBits(const esp32irpk::IRDecodedBits& in);
  esp32irpk::IRDecodedBits toBits() const;
};

}
```

Frame types are not the decode/encode implementation. Decode/encode is handled by codec and `IRProtocolSpec`.

Every built-in protocol provides a Frame type with both `fromBits()` and `toBits()`, so a decoded frame can be inspected and re-sent symmetrically.

## 8. BITS Helpers

Every built-in protocol has a small helper under `esp32irpk::bits` that returns `IRDecodedBits`. Helpers mirror each protocol's Frame fields and are the shortest construction route.

```cpp
esp32irpk::IRDecodedBits bits = esp32irpk::bits::nec(0x00ff, 0x34);
tx.send(bits);
```

| Helper | Fields |
|---|---|
| `bits::nec(address, command)` / `bits::necRepeat()` | NEC |
| `bits::aeha(data, bit_length)` | AEHA (variable length) |
| `bits::sony12(data)` / `bits::sony15(data)` / `bits::sony20(data)` | Sony SIRC |
| `bits::samsung32(address, command)` / `bits::samsung36(address, command)` | Samsung |
| `bits::jvc(address, command)` | JVC |
| `bits::rc5(data)` / `bits::rc6m0(data)` / `bits::rc6m6(data)` | RC5 / RC6 raw payload |

Helpers are not send APIs. Sending is always handled by `IRSender::send()`.

## 9. Return Values And Safety

- Exceptions are not used.
- Success/failure is returned as `bool`.
- Calls that violate begin-before/after constraints return `false`.
- `out` is undefined when `read()` returns `false`.
- APIs must not be called from ISR context.
- Concurrent calls to the same instance from multiple threads are undefined.

## 10. Minimal Example

```cpp
#include <ESP32IRPulseKit.h>

esp32irpk::IRReceiver<> rx(4, true);
esp32irpk::IRSender tx(5);

void setup() {
  Serial.begin(115200);
  rx.begin();
  tx.begin();
}

void loop() {
  esp32irpk::IRReceiveResult<> result;
  if (!rx.read(result)) {
    return;
  }

  if (const esp32irpk::IRDecodedBits* bits = result.bits()) {
    tx.send(bits);
  }
}
```

## 11. Air-Conditioner Support

Air-conditioner / heat-pump remotes send a whole multi-byte state in one button press (often 100–300+ bits, vendor-specific layout with a checksum, sometimes several frames per press). This does not fit the generic codec's 64-bit single-frame `IRDecodedBits` model, so AC support is a **separate layer** under `esp32irpk::ac` that works on the RAW tick path. It does not touch `IRDecodedBits`, the candidate scorer, or `IRProtocolID`, and AC vendors are never auto-registered.

### 11.1 Learn And Replay (vendor-independent)

Any AC waveform can be captured and re-sent without decoding it:

- Receive in RAW-only mode (`setDecodeCandidates(0)`), with `setMaxRxSymbols()` large enough for the frame and `setIdleThresholdUs()` large enough to span the frame's internal gaps so the whole burst is one capture.
- `read()` returns the full burst as one `IRRawTickView` (RAW-only mode does not split).
- Re-send the captured RAW with `IRSender::send(const IRRawTickView&)`. For AC frames use the phase-aligned carrier (the default; §11.3) — it is the safe choice across vendors, since some (e.g. Gree) drop frames on the hardware carrier.

### 11.2 Decode And Encode (per vendor)

Decoding into meaningful fields and regenerating a frame is handled per vendor. Each vendor lives in its own namespace `esp32irpk::ac::<Vendor>` and exposes the same structure — `Mode`, `Fan`, and `Frame` — converting between RAW ticks and a byte-structured logical state, mirroring the generic `frames::*` `fromBits`/`toBits` pattern but RAW-based and byte-wide.

`Mode` and `Fan` are **per-vendor enums**, not a single shared enum: each contains only the values that vendor actually supports, so an unsupported setting cannot even be named. Common members follow a shared naming convention (`AUTO`/`COOL`/`HEAT`/`DRY`/`FAN`, and fan steps where they exist) so every vendor reads the same. The naming *structure* is identical across vendors (`ac::<Vendor>::Mode::COOL`); the *member set* is vendor-specific.

```cpp
namespace esp32irpk::ac {

enum class AcVendor : uint16_t {
  UNKNOWN = 0,
  PANASONIC = 1,
  GREE = 2,
  // further vendors added incrementally
};

namespace Panasonic {

// Per-vendor: only the values Panasonic supports. Common members use the
// shared naming convention; this enum does not allow a value Panasonic lacks.
enum class Mode : uint8_t { AUTO = 0, COOL, HEAT, DRY, FAN };
// Arduino defines LOW/HIGH as macros, so fan steps use the `_SPEED` suffix.
enum class Fan  : uint8_t { AUTO = 0, QUIET, LOW_SPEED, MED_SPEED, HIGH_SPEED, POWERFUL };

struct Frame {
  static constexpr size_t kMaxTicks = /* vendor frame upper bound */;

  uint8_t bytes[kBytes] = {}; // raw decoded state (the intermediate form)
  uint16_t byte_length = 0;
  bool checksum_ok = false;
  // When this wire format has model variants (e.g. Gree YBOFB/YAW1F), the Frame
  // also carries a `Model model` field: fromRaw sets it, toRaw/accessors honor
  // it. Formats with a single model omit it. See "Two axes of variation" below.

  // logical accessors over `bytes`
  bool power() const;          void setPower(bool on);
  Mode mode() const;           void setMode(Mode m);
  uint8_t temperatureC() const; void setTemperatureC(uint8_t c);
  Fan fan() const;             void setFan(Fan f);

  static bool fromRaw(const esp32irpk::IRRawTickView& raw, Frame& out);
  bool toRaw(esp32irpk::IRRawTickBuffer& out) const;
};

} // namespace Panasonic

// Convenience: encode `frame` (any vendor Frame) and transmit it in one call.
template <class Frame>
bool send(esp32irpk::IRSender& tx, const Frame& frame);

}
```

- `Frame::fromRaw(raw, out)` decodes RAW ticks into the state bytes and validates the vendor checksum. It returns `false` when the waveform is not that vendor's frame; `out.checksum_ok` reports checksum validity separately.
- `Frame::toRaw(out)` recomputes the checksum and renders the state to RAW ticks in the caller-provided `IRRawTickBuffer`. Send the result with `IRSender::send(const IRRawTickView&)`. It returns `false` if `model` names a variant whose field map is not implemented — encoding an unsupported model fails rather than silently emitting the implemented model's layout. `ac::send` propagates this and also returns `false`.
- `ac::send(tx, frame)` is the one-call path: it encodes into a stack buffer of `Frame::kMaxTicks` and transmits, returning `false` on encode or send failure. Use the explicit `toRaw` + `IRSender::send()` pair instead when you need to control the buffer. The sender's carrier mode is configured separately, as usual (use the phase-aligned default for AC; see §11.3).
- The byte array is the intermediate form. Logical fields (power, mode, temperature, fan, …) are accessors over those bytes.
- Every vendor follows this same structure under its own `esp32irpk::ac::<Vendor>` namespace. A per-vendor enum-to-name helper (e.g. `Panasonic::toString(Mode)`) may be added later; it is not required by the core contract.
**Two axes of variation.** A vendor's remotes differ in two independent ways, and the layer represents them differently:

- **Format (wire protocol)** — a different frame length, timing, header, or checksum is a genuinely different waveform that needs its own parser, so it is a **separate `Frame` type**. A vendor with several wire formats exposes one `Frame` type per format under its namespace (e.g. the 18-byte "Mitsubishi AC" protocol is one type; the shorter Mitsubishi 136 / 112 protocols would be distinct types).
- **Model** — when remotes share one wire format and differ only by a model-identifier bit or a few field encodings, that is a **`Model` parameter** on the single `Frame` type, not a separate type. `fromRaw` detects the model from the captured bytes (frame length / checksum / model bit) and records it in `Frame::model`; `toRaw` and the accessors honor it. Adding a model is additive — a new enum value plus its field handling — with no API change.

```cpp
namespace Gree {
enum class Model : uint8_t { YBOFB = 0, YAW1F, YX1FSF }; // variants of one wire format
struct Frame {
  // ...bytes / checksum_ok as above...
  Model model = Model::YBOFB; // fromRaw detects it; toRaw + accessors honor it
};
}
```

This is why a model is a parameter rather than a type-per-model: a received frame must resolve its own model on `fromRaw`, so the decoder cannot require the caller to pick the class first. A new wire *format* always gets its own `Frame` type because it cannot share a parser.

**Vendor / format / model support matrix.** "Supported" means implemented and hardware-verified (bidirectionally against IRremoteESP8266, plus a HeatpumpIR second reference). "Planned" / "Not yet" formats and models are recognized but not implemented; their target models are decided before implementation, not pre-locked here.

| Vendor | Format (protocol) | Frame | Model(s) | Status |
|---|---|---|---|---|
| Panasonic | Kaseikyo AC | 27-byte, two frames | JKE | **Supported** |
| | | | DKE / NKE / LKE / CKP / RKR | Not yet |
| | Panasonic-AC32 | short 32-bit | — | Not yet |
| Gree | Gree | 8-byte, two blocks | YBOFB | **Supported** |
| | | | YAW1F / YX1FSF | Not yet |
| Mitsubishi | Mitsubishi AC | 18-byte | single | **Supported** |
| | Mitsubishi 136 | 17-byte | — | Not yet |
| | Mitsubishi 112 | 14-byte | — | Not yet |
| | Mitsubishi Heavy | 88 / 152-bit | — | Not yet |
| Fujitsu | Fujitsu AC | 16-byte long / 7-byte short | ARRAH2E … ARREW4E | Planned (model TBD) |
| Daikin | Daikin (+ size variants) | 35-byte + others | — | Planned (model TBD) |

Per-vendor framing of the supported formats:

- `Panasonic` — Kaseikyo/AEHA family: two pulse-distance frames (8-byte signature + 19-byte state), LSB-first, sum checksum over the second frame. The implemented model is `Model::JKE` (its template is byte-identical to IRremoteESP8266's default known-good state); DKE/NKE/LKE/CKP/RKR differ in fixed marker bytes and are reserved.
  - Known unimplemented field (0.5°C): the integer °C lives at `kOffTemp` (overall byte 14, `c<<1`), but the **half-degree bit is the separate overall byte 22 (frame-2 byte 14), bit 7 (0x80)**. Two captures from a real JKE remote at 23.0°C vs 23.5°C differ only in byte 22 bit 7 and the checksum (byte 26) (e.g. `…40 80 80 …16 D0` = 23.0°C vs the same with byte 22 `00`→`80` and byte 26 `D0`→`50` = 23.5°C). The current `temperatureC()` reads only `byte14>>1`, so it reports 23°C for both; the 0.5°C is not yet decoded/encoded. The bit position is confirmed from one remote / two samples, but generalizing the half-degree field across all temperatures and other models needs more captures.
- `Gree` — one 8-byte state sent as two pulse-distance blocks. The first block carries bytes 0–3 plus a fixed 3-bit footer; the second carries bytes 4–7 with no header of its own. A Kelvinator-style block checksum occupies the high nibble of byte 7. The implemented model is YBOFB (`Model::YBOFB`, model bit clear); `Model::YAW1F`/`YX1FSF` are reserved for later. `Fan` is `AUTO`/`MIN_SPEED`/`MED_SPEED`/`MAX_SPEED`.
- `Mitsubishi` — the 18-byte "Mitsubishi AC" protocol (MSZ/Kirigamine remotes): one pulse-distance frame with a fixed 5-byte signature, sent twice with a long gap; the last byte is a sum checksum over the rest. This format has a single model (no `Model` parameter); the other Mitsubishi wire formats (136 / 112 / Heavy) would be separate `Frame` types. `Fan` is `AUTO`/`QUIET`/`LOW_SPEED`/`MED_SPEED`/`HIGH_SPEED`/`MAX_SPEED`.

AC types are not send APIs. Sending is always handled by `IRSender::send()`.

### 11.3 Carrier For Long Frames

AC frames are long, and the carrier that delivers them reliably depends on the vendor's timing margins. The library carrier when you do not call `setPhaseAlignedCarrier` is the phase-aligned one (§6.5).

The two carriers trade precision for size. The **phase-aligned** carrier emits each mark as whole carrier-cycle symbols: every mark is an exact integer number of cycles, with no ±1-cycle wobble, but a multi-frame burst expands to several thousand RMT symbols (~17 KB allocated transiently per send) and the larger stream raises the refill-underrun risk under heavy interrupt load. The **hardware** (free-running) carrier uses far fewer symbols, but each mark edge can land up to one carrier period late (~26 µs at 38 kHz).

That wobble matters for tightly-timed vendors:

- **Panasonic** tolerates either carrier — both delivered every frame on the test rig — so the hardware carrier is fine and cheaper.
- **Gree** requires the phase-aligned carrier. Its zero-space (540 µs) is shorter than its bit mark (620 µs), so the hardware carrier's mark wobble pushes spaces out of tolerance and the receiver rejects about half the frames (measured: phase-aligned 50/50 vs hardware ~55%).
- **Mitsubishi** is the same tight-timing case (zero-space 420 µs < bit mark 450 µs) and likewise uses the phase-aligned carrier.

Recommendation: the phase-aligned carrier is the safe default for AC, and is what you get if you never call `setPhaseAlignedCarrier`. Use the hardware carrier (`setPhaseAlignedCarrier(false)`) only as a memory optimization for loosely-timed vendors such as Panasonic. The carrier affects delivery rate, not byte integrity — a received frame is always byte-correct because it is checksum-validated, and the phase-aligned carrier is not size-limited (durations beyond the 15-bit field are split across symbols).
