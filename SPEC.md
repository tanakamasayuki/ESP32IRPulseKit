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
// QUIET/POWERFUL are not speeds but comfort modes (fan stays auto + a byte21
// flag); they are mutually exclusive with a speed, so they are values of the one
// Fan selector.
enum class Fan : uint8_t {
  AUTO = 0, MIN_SPEED, LOW_SPEED, MED_SPEED, HIGH_SPEED, MAX_SPEED, QUIET, POWERFUL
};
// Panasonic-specific: vertical louver position (low nibble of the fan byte). AUTO sweeps.
enum class Louver : uint8_t { AUTO = 0xF, P1 = 1, P2, P3, P4, P5 };

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
  float temperatureC() const; void setTemperatureC(float c); // 0.5C steps, symmetric get/set
  Fan fan() const;             void setFan(Fan f);
  // Panasonic-specific: temperatureC()/setTemperatureC are a symmetric float pair
  // carrying the 0.5C. halfDegree() is a convenience for just the +0.5 bit.
  bool halfDegree() const;
  Louver louver() const;       void setLouver(Louver v);

  static bool fromRaw(const esp32irpk::IRRawTickView& raw, Frame& out);
  static bool fromBytes(const uint8_t* state, size_t len, Frame& out); // rebuild from decoded state
  bool toRaw(esp32irpk::IRRawTickBuffer& out) const;
  void printTo(Print& out) const; // diagnostic dump: common + vendor fields + hex
};

} // namespace Panasonic

// Convenience: encode `frame` (any vendor Frame) and transmit it in one call.
template <class Frame>
bool send(esp32irpk::IRSender& tx, const Frame& frame);

// Try every built-in AC vendor against a RAW capture; returns which matched
// (UNKNOWN if none). With a non-null Print, the matched frame is also dumped via
// printTo() (and a no-match note is printed).
AcVendor decodeAny(const esp32irpk::IRRawTickView& raw, Print* out = nullptr);

// Print copy-paste C++ that rebuilds the matched frame from its decoded state
// bytes (the compact, bit-exact alternative to a RAW tick dump). Returns the
// matched vendor, or UNKNOWN if none (nothing printed — fall back to RAW).
AcVendor printSendSnippet(const esp32irpk::IRRawTickView& raw, Print& out);

// Print an editable setter template (f.setMode(...); f.setTemperatureC(...); ...)
// for the matched frame — easy to tweak before sending, but LOSSY (fields with
// no setter reset to defaults; not bit-exact — use printSendSnippet for that).
AcVendor printSetterSnippet(const esp32irpk::IRRawTickView& raw, Print& out);

}
```

- `Frame::fromRaw(raw, out)` decodes RAW ticks into the state bytes and validates the vendor checksum. It returns `false` when the waveform is not that vendor's frame; `out.checksum_ok` reports checksum validity separately.
- `Frame::fromBytes(state, len, out)` rebuilds a frame from its `kBytes` decoded state without going through RAW ticks (it classifies the model and reports `checksum_ok`, like `fromRaw`); `len` must equal `Frame::kBytes`. It is the compact, bit-exact counterpart to a RAW replay: `fromBytes` → `toRaw`/`ac::send` reproduces the captured frame byte-for-byte (only the deterministic signature/preamble/markers/checksum are re-derived). `ac::printSendSnippet` emits a copy-paste snippet built on it. (A frame rebuilt from logical setters instead would *not* be bit-exact — fields without setters, e.g. timers, fall back to template defaults.)
- `Frame::toRaw(out)` recomputes the checksum and renders the state to RAW ticks in the caller-provided `IRRawTickBuffer`. Send the result with `IRSender::send(const IRRawTickView&)`. It returns `false` if `model` names a variant whose field map is not implemented — encoding an unsupported model fails rather than silently emitting the implemented model's layout. `ac::send` propagates this and also returns `false`.
- `ac::decodeAny(raw, out)` tries every built-in AC vendor against the RAW capture in registration order and returns the matching `AcVendor` (`UNKNOWN` if none). When `out` is non-null the matched frame is dumped via its `printTo()`. It reports only the vendor identity, not the decoded frame — the per-vendor `Frame`s are heterogeneous types, so decode the specific vendor when you need its fields or to re-encode. Centralizing the cascade here means a learn/dump path picks up new vendors automatically.
- `ac::printSendSnippet(raw, out)` decodes the matching vendor and prints copy-paste C++ that rebuilds the frame via `Frame::fromBytes` and sends it — the compact, bit-exact alternative to a RAW tick array (a 27/18/8-byte state vs. hundreds of ticks). Returns the matched `AcVendor`, or `UNKNOWN` if none matched (nothing printed; fall back to a RAW snippet). Mirrors `decodeAny` so the vendor list stays in one place.
- `ac::printSetterSnippet(raw, out)` instead prints an *editable* template built from the logical setters (`f.setMode(...)`, `f.setTemperatureC(...)`, enum values via `toString`) — the practical "tweak a value and re-send" form. It is **lossy**: fields without a setter (timers, vendor flags) revert to template defaults, so it is not bit-exact (use `printSendSnippet` when fidelity matters). Same return convention. Generic protocols have the analogous `esp32irpk::debug::printFrameStructSnippet` (one-line `bits::*` helper plus the editable `frames::*Frame` struct).
- `ac::send(tx, frame)` is the one-call path: it encodes into a stack buffer of `Frame::kMaxTicks` and transmits, returning `false` on encode or send failure. Use the explicit `toRaw` + `IRSender::send()` pair instead when you need to control the buffer. The sender's carrier mode is configured separately, as usual (use the phase-aligned default for AC; see §11.3).
- The byte array is the intermediate form. Logical fields (power, mode, temperature, fan, …) are accessors over those bytes.
- `Frame::printTo(Print& out)` is a diagnostic dump: it writes the common `power/mode/temp/fan/checksum` line, the vendor's own fields (louver / swing / vane), and the full state in hex to any Arduino `Print` (e.g. `Serial`). Enum fields print as their raw code. It is a convenience for learn/dump sketches, not part of the encode/decode contract.
- Every vendor follows this same structure under its own `esp32irpk::ac::<Vendor>` namespace. Each vendor also provides `toString(...)` overloads that map its enums (`Mode`/`Fan` and any swing/louver/vane enum) to the bare enumerator name (e.g. `"MIN_SPEED"`), used by `printTo` and available for logging; unknown values return `"?"`. The bare name is the reusable form — prepend `esp32irpk::ac::<Vendor>::<Enum>::` when emitting code. These are convenience helpers, not part of the core contract.
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
| Panasonic | Kaseikyo AC | 27-byte, two frames | JKE / DKE / NKE / LKE / RKR | **Supported** |
| | | | CKP | Not yet |
| | Panasonic-AC32 | short 32-bit | — | Not yet |
| Gree | Gree | 8-byte, two blocks | YBOFB | **Supported** |
| | | | YAW1F / YX1FSF | Not yet |
| Mitsubishi | Mitsubishi AC | 18-byte | single | **Supported** |
| | Mitsubishi 136 | 17-byte | — | Not yet |
| | Mitsubishi 112 | 14-byte | — | Not yet |
| | Mitsubishi Heavy | 88 / 152-bit | — | Not yet |
| Fujitsu | Fujitsu AC | 16-byte long / 7-byte short | ARRAH2E | **Supported** |
| | | | ARDB1 / ARJW2 / ARREB1E / ARRY4 / ARREW4E | Not yet |
| Daikin | Daikin classic (ARC433) | 35-byte, 3 sections | single | **Supported** |
| | Daikin2 / 216 / 160 / 176 / 128 / 152 / 64 / 312 | various sizes | — | Not yet |
| Toshiba | Toshiba AC | 9-byte | standard | **Supported** |
| | short (7-byte swing) / long (10-byte) | — | Not yet |
| Samsung | Samsung AC | 14-byte, 2 sections | standard | **Supported**² |
| | extended (21-byte timer) | — | Not yet |
| Sharp | Sharp AC | 13-byte | A907 | **Supported**¹ |
| | | A705 / A903 | Not yet |
| Kelvinator | Kelvinator | 16-byte, two blocks | standard | **Supported**³ |
| Midea | Midea | 48-bit (6-byte), 2 copies | standard | **Supported**⁴ |
| Carrier | Carrier | 64-bit (8-byte) | CARRIER_AC64 | **Supported**⁵ |

¹ Sharp (13-byte, A907 model), like Samsung, is verified by the IRremoteESP8266 bidirectional pair (`sharp_irremoteesp8266_tx` / `_rx` — encode and decode each checked against an independent stack on hardware) rather than the usual IRremoteESP8266 + HeatpumpIR combination, because HeatpumpIR has no Sharp support.

² Samsung is verified by the IRremoteESP8266 bidirectional pair (`samsung_irremoteesp8266_tx` / `_rx` — encode and decode each checked against an independent stack on hardware) rather than the usual IRremoteESP8266 + HeatpumpIR combination: HeatpumpIR's Samsung classes implement the older AQV (21-byte) and FJM (different section-2 checksum) variants, neither matching the modern 14-byte SAMSUNG_AC.

³ Kelvinator (16-byte, standard), like Samsung and Sharp, is verified by the IRremoteESP8266 bidirectional pair (`kelvinator_irremoteesp8266_tx` / `_rx` — encode and decode each checked against an independent stack on hardware) rather than the usual IRremoteESP8266 + HeatpumpIR combination, because HeatpumpIR has no Kelvinator support. The two-block framing (per-block B010 command footer + gaps) and block checksums are additionally checked in host `codec_smoke`, and it passes the PulseKit self round-trip on hardware (`hardware/protocol_matrix_ac`).

⁴ Midea (48-bit / 6-byte, standard), like Samsung, Sharp and Kelvinator, is verified by the IRremoteESP8266 bidirectional pair (`midea_irremoteesp8266_tx` / `_rx` — encode and decode each checked against an independent stack on hardware) rather than the usual IRremoteESP8266 + HeatpumpIR combination, because HeatpumpIR has no Midea support. The double-transmission framing (the 48 data bits followed by a bit-inverted copy) and the checksum are additionally checked in host `codec_smoke`, and it passes the PulseKit self round-trip on hardware (`hardware/protocol_matrix_ac`).

⁵ Carrier (64-bit / 8-byte, CARRIER_AC64), like Samsung, Sharp, Kelvinator and Midea, is verified by the IRremoteESP8266 bidirectional pair (`carrier_irremoteesp8266_tx` / `_rx` — encode and decode each checked against an independent stack on hardware) rather than the usual IRremoteESP8266 + HeatpumpIR combination: HeatpumpIR has a Carrier class, but it implements the different NQV (9-byte) / MCA (6-byte) Carrier variants rather than CARRIER_AC64. The single-frame pulse-distance framing and the 4-bit nibble-sum checksum are additionally checked in host `codec_smoke`, and it passes the PulseKit self round-trip on hardware (`hardware/protocol_matrix_ac`).

**Candidate vendors (not yet started), in suggested implementation order.** Ordering weighs, in priority: a second independent reference (IRremoteESP8266 *and* HeatpumpIR, matching the verification used for the supported vendors), a clean byte-state fit for the `ac::` layer, framing simplicity, and device coverage. Target models are chosen at approval time, not pre-locked here. Bit-paired code formats (Coolix 24-bit, LG 28-bit, …) are deliberately out of scope — they are not multi-byte byte-state and would need a different code path outside this layer.

| Vendor | Format / size | References | Notes |
|---|---|---|---|
| Hitachi | Hitachi AC, 28-byte (+ 13–53-byte variants) | IRremoteESP8266 + HeatpumpIR | Hardest: many size variants with leader/section framing. Defer until the simpler vendors are done. |

Further single-reference (IRremoteESP8266-only) byte-state options if more coverage is wanted: Haier (9-byte / 22-byte), TCL112 (14-byte), Electra (13-byte).

Per-vendor framing of the supported formats:

- `Panasonic` — Kaseikyo/AEHA family: two pulse-distance frames (8-byte signature + 19-byte state), LSB-first, sum checksum over the second frame. `Model::JKE` (template byte-identical to IRremoteESP8266's default known-good state), `DKE`, `NKE`, `LKE` and `RKR` are supported — they share the power/mode/temperature/fan field map and differ only in fixed marker bytes (`fromRaw` classifies the model; `toRaw` stamps it), verified per-model against IRremoteESP8266. `CKP` is reserved (toggle power + relocated quiet/powerful bits); encoding it returns `false`.
- `Gree` — one 8-byte state sent as two pulse-distance blocks. The first block carries bytes 0–3 plus a fixed 3-bit footer; the second carries bytes 4–7 with no header of its own. A Kelvinator-style block checksum occupies the high nibble of byte 7. The implemented model is YBOFB (`Model::YBOFB`, model bit clear); `Model::YAW1F`/`YX1FSF` are reserved for later. `Fan` is `AUTO`/`MIN_SPEED`/`MED_SPEED`/`MAX_SPEED`; `SwingV` and `SwingH` set the vertical and horizontal swing.
- `Mitsubishi` — the 18-byte "Mitsubishi AC" protocol (MSZ/Kirigamine remotes): one pulse-distance frame with a fixed 5-byte signature, sent twice with a long gap; the last byte is a sum checksum over the rest. This format has a single model (no `Model` parameter); the other Mitsubishi wire formats (136 / 112 / Heavy) would be separate `Frame` types. `Fan` is `AUTO`/`QUIET`/`LOW_SPEED`/`MED_SPEED`/`HIGH_SPEED`/`MAX_SPEED`; `Vane` (vertical, `P1`..`P5`) and `WideVane` (horizontal) set the airflow direction, and `temperatureC()`/`setTemperatureC()` are a symmetric `float` pair carrying 0.5 °C steps.
- `Fujitsu` — the "Fujitsu AC" protocol (AR-series remotes), targeting model ARRAH2E. A full setting is a 16-byte pulse-distance "long" frame beginning with the fixed bytes `14 63 00 10 10`, byte 5 = `0xFE` (the long-frame marker), and a complement checksum in byte 15; a power-off is the 7-byte "short" frame `14 63 00 10 10 02 FD` (byte 6 = `~`byte 5). Each frame is sent once. Power is carried by the frame type (long = on, short OFF = off), not a state bit, so `setPower(false)` emits the short frame and the don't-care mode/temp/fan fields of a decoded OFF frame keep the template defaults. Single model (no `Model` parameter yet); ARDB1 / ARJW2 / ARREB1E / ARRY4 / ARREW4E differ in length, marker, checksum complement, and (ARREW4E) temperature encoding and would be added as model branches or `Frame` types later. `Fan` is `AUTO`/`HIGH_SPEED`/`MED_SPEED`/`LOW_SPEED`/`QUIET`; `Swing` is `OFF`/`VERTICAL`/`HORIZONTAL`/`BOTH`.
- `Daikin` — the classic "Daikin" / ARC433 protocol (ARC433** / ARC466 remotes, M-Series / FTXM-M units). A 35-byte state is sent as a 5-bit `00000` leading preamble followed by **three** pulse-distance sections (8 + 8 + 19 bytes), each with its own header and a per-section sum checksum (bytes 7 / 15 / 34); every section begins with the fixed signature `11 DA 27`. Single classic wire format (no `Model` parameter); the other Daikin sizes (Daikin2 / 216 / 160 / 176 / 128 / 152 / 64 / 312) are separate `Frame` types. `Mode` is `AUTO`/`DRY`/`COOL`/`HEAT`/`FAN`; `Fan` is `AUTO`/`QUIET`/`MIN_SPEED`/`LOW_SPEED`/`MED_SPEED`/`HIGH_SPEED`/`MAX_SPEED`; `setSwingVertical`/`setSwingHorizontal` toggle the two airflow axes; `temperatureC()`/`setTemperatureC()` are a `float` pair (byte 22 stores °C × 2). It must use the phase-aligned carrier — its zero-space equals its bit mark (both 428 µs), §11.3.
- `Toshiba` — the standard "Toshiba AC" protocol (WH-/RAS- remotes, rebadged Carrier units). A 9-byte pulse-distance frame transmitted twice (separated by the frame gap), **MSB-first** (the only MSB-first AC vendor), beginning with the fixed signature `F2 0D` (byte 1 = `~`byte 0; byte 3 = `~`byte 2), with an XOR checksum (of bytes 0–7) in byte 8. Power is carried by the Mode field (`Mode == 7` = off), not a separate bit, so `setPower(false)` writes mode 7 and `setPower(true)` restores the last set mode. `Mode` is `AUTO`/`COOL`/`DRY`/`HEAT`/`FAN`; `Fan` is `AUTO`/`MIN_SPEED`/`LOW_SPEED`/`MED_SPEED`/`HIGH_SPEED`/`MAX_SPEED` (whole-degree temps, 17–30 °C). Swing is a separate short-message variant (reserved); the standard frame does not carry it. The 7-byte short and 10-byte long messages are separate `Frame` types.
- `Samsung` — the standard "Samsung AC" protocol (AR-/ARH- series remotes). A 14-byte state, **LSB-first**, sent as a one-time leading header (690 µs mark + 17844 µs space) followed by two 7-byte sections, each with its own section header (3086/8864 µs) and a 2886 µs section gap. Each section carries a popcount (Hamming-weight) checksum, bitwise-inverted, split across two nibbles of its bytes 1–2. There is no fixed vendor signature, so `fromRaw` gates on both section checksums validating. Power is two 2-bit fields (byte 6 and byte 13): both `0b11` = on, both `0b00` = off. `Mode` is `AUTO`/`COOL`/`DRY`/`FAN`/`HEAT`; `Fan` is `AUTO`/`LOW_SPEED`/`MED_SPEED`/`HIGH_SPEED`/`MAX_SPEED` (whole-degree temps, 16–30 °C). Swing and the special fan flags (Powerful/WindFree/Econo) are not settable here. The 21-byte extended (timer) message is a separate `Frame` type.
- `Sharp` — the standard "Sharp AC" protocol. A 13-byte single pulse-distance frame, **LSB-first**, beginning with the fixed header `AA 5A CF 10`, with a nibble-folded XOR checksum in the high nibble of byte 12. Power lives in the 4-bit `PowerSpecial` field (byte 5): on = 3, off = 2 (not a single bit). The `Special` byte (byte 10) records which button a real remote pressed; `toRaw` always emits the "power" value (0x00) so it is a complete state command. `Mode` is `AUTO`/`HEAT`/`COOL`/`DRY` (Auto and Fan share wire code `0b00`, so no standalone Fan); `Fan` is `AUTO`/`MIN_SPEED`/`MED_SPEED`/`HIGH_SPEED`/`MAX_SPEED` (non-contiguous wire codes 2/4/3/5/7; whole-degree temps 15–30 °C). Auto and Dry modes carry no temperature (Temp = 0), so temperature is a don't-care there. This is the default A907 model; A705 / A903 (which remap Heat to Fan and use different fan codes, flagged by the Model/Model2 bits) are reserved. Swing, ion, clean and timer are documented but not settable.
- `Kelvinator` — the standard "Kelvinator" protocol (also used by some Gree/Sharp-badged remotes). 16 bytes = two 8-byte blocks, **LSB-first**. Each block is a header + 32 data bits + a 3-bit command footer (`B010`) + a ~20 ms gap + 32 more data bits + a ~40 ms gap; bytes 8–10 repeat bytes 0–2, byte 3 / byte 11 are fixed markers (`0x50` / `0x70`), and each block ends with a 4-bit block checksum (high nibble of byte 7 / byte 15, Gree-style: 10 + low nibbles of the first four bytes + high nibbles of the next three, mod 16). `Mode` is `AUTO`/`COOL`/`DRY`/`FAN`/`HEAT`; `Fan` is `AUTO`/`MIN_SPEED`/`LOW_SPEED`/`MED_SPEED`/`HIGH_SPEED`/`MAX_SPEED` (the encoder mirrors the low speeds into the byte-0 BasicFan field, capped at 3); whole-degree temps 16–30 °C (Auto/Dry force 25 °C). Single format, no model axis. Vertical/horizontal swing, turbo, quiet, light, ion filter and X-Fan are documented but not settable.
- `Midea` — the standard "Midea" protocol (used by many OEM-rebadged brands: Pioneer, Comfee, Kaysun, Keystone, MrCool, Danby, Trotec, Lennox, …). 48 bits / 6 bytes, **MSB-first**, sent as two transmissions per message: the 48 data bits, then the same 48 bits fully **bit-inverted** (each with its own 4480/4480 header, 560/1680 bits, and a 5.6 ms gap). Byte order is transmission order (byte 0 = the fixed Header/Type byte `0xA1`, byte 5 = the checksum); this is the reverse of IRremoteESP8266's `remote_state` union. The checksum is the negated sum of the bit-reversed other five bytes, itself bit-reversed. `fromRaw` gates on the second copy being the exact bit-complement of the first plus the fixed Header field (`0b10100`). `Mode` is `COOL`/`DRY`/`AUTO`/`HEAT`/`FAN`; `Fan` is `AUTO`/`LOW_SPEED`/`MED_SPEED`/`HIGH_SPEED` (only four speeds); whole-degree temps 17–30 °C, carried in every mode. Celsius only (the Fahrenheit flag is forced clear). Single format, no model axis. Sleep, on/off timers, the sensor/follow-me message and the special toggle messages (swing, econo, turbo, light, clean, 8 °C-heat, quiet) are documented but not settable.
- `Carrier` — the CARRIER_AC64 protocol (Carrier/Surrey 619EGX / 53NGK inverter remotes). A single 8-byte (64-bit) pulse-distance frame, **LSB-first**, sent once, beginning with the fixed signature `0x84 0x55`, with a 4-bit checksum in the low nibble of byte 2 (the sum of every nibble above it — byte 2's high nibble plus bytes 3–7). `Mode` is `HEAT`/`COOL`/`FAN` (no Auto or Dry); `Fan` is `AUTO`/`LOW_SPEED`/`MED_SPEED`/`HIGH_SPEED`; whole-degree temps 16–30 °C, carried in every mode. `SwingV` is settable. This is the 64-bit format; the other Carrier wire formats (AC / AC40 / AC84 / AC128) are separate frames. Sleep and the on/off timers are documented but not settable.

**Panasonic field map (decoded logical fields).** Where each control field lives in the 27-byte state. Status legend: ✅ implemented (decode + encode) · 🔜 planned · 🟡 documented, no setter (re-send via RAW replay) · ⛔ out of scope (separate frame type).

| Field | Location (byte/bit) | Code / range | Status |
|---|---|---|---|
| power | byte 13 bit 0 | on=1 / off=0 | ✅ |
| mode | byte 13 high nibble | cool=3 / dry=2 / heat=4 (auto=0 / fan=6) | ✅ |
| temperature (integer) | byte 14 | `floor(°C) << 1`, 16–30 °C | ✅ |
| half-degree (0.5 °C) | byte 22 bit 7 | set = +0.5 °C | ✅ |
| fan (airflow) | byte 16 high nibble | auto=A / 3..7 (low→max) | ✅ |
| louver (swing) | byte 16 low nibble | 1–5 = fixed / F = auto | ✅ |
| quiet / powerful | byte 21 bit 5 / bit 0 | quiet=0x20 / powerful=0x01 | ✅ (as `Fan::QUIET`/`POWERFUL`) |
| on/off timer | byte 13 bit 1,2 + bytes 18–20 | 11-bit minutes | 🟡 |
| checksum | byte 26 | sum of frame-2 bytes 8–25 mod 256 | ✅ |

The newly mapped fields (half-degree, louver, quiet/powerful, timer) were reverse-engineered from one real remote (part ACXA75C15870, a JKE-family unit) and are not yet cross-validated across models/libraries, unlike power/mode/temperature/fan/checksum (verified against IRremoteESP8266). Quiet/powerful are mutually exclusive with a fan speed: selecting them forces the fan nibble to auto and sets the byte-21 flag. They are therefore values of the one `Fan` selector — `Fan::QUIET`/`Fan::POWERFUL` encode to fan-nibble=auto + the byte-21 bit, and the speeds `MIN_SPEED`..`MAX_SPEED` are the nibble (the no-setter policy for timers is in [DESIGN.md](DESIGN.md)). Special-function buttons (e.g. internal clean) are sent as a separate, shorter command frame (⛔ a different `Frame` type, not yet supported); RAW replay still reproduces them.

**Gree field map (decoded logical fields).** Where each control field lives in the 8-byte state (two 4-byte blocks). Same status legend.

| Field | Location (byte/bit) | Code / range | Status |
|---|---|---|---|
| mode | byte 0 bits 0-2 | auto=0 / cool=1 / dry=2 / fan=3 / heat=4 | ✅ |
| power | byte 0 bit 3 | on=1 / off=0 | ✅ |
| fan (airflow) | byte 0 bits 4-5 | auto=0 / 1–3 (min→max) | ✅ |
| swing vertical | byte 0 bit 6 (auto) + byte 4 bits 0-3 (position) | auto / pos 1–7, 9, 11 | ✅ |
| sleep | byte 0 bit 7 | on=1 | 🟡 |
| temperature | byte 1 bits 0-3 | `°C − 16`, 16–30 °C | ✅ |
| timer | byte 1 bits 4-7 + byte 2 bits 0-3 | enabled + tens-hour / half-hour / hours | 🟡 |
| turbo | byte 2 bit 4 | on=1 | 🟡 |
| light | byte 2 bit 5 | on=1 | 🟡 |
| model marker | byte 2 bit 6 | YAW1F=1 (YBOFB=0) | model param |
| xfan | byte 2 bit 7 | on=1 | 🟡 |
| Fahrenheit | byte 3 bit 3 (+ bit 2 extra 0.5 °F) | °C / °F unit | 🟡 |
| swing horizontal | byte 4 bits 4-6 | off / auto / left…right | ✅ |
| display-temp source | byte 5 bits 0-1 | off / set / inside / outside | 🟡 |
| iFeel | byte 5 bit 2 | on=1 | 🟡 |
| WiFi | byte 5 bit 6 | on=1 | 🟡 |
| econo | byte 7 bit 2 | on=1 | 🟡 |
| checksum | byte 7 bits 4-7 | Kelvinator block nibble-sum | ✅ |

Byte 3's high nibble (`0b0101`) and byte 5 bits 3-5 (`0b100`) are fixed markers the remote always carries (kept in the frame template). Both swing axes have setters (`SwingV` / `setSwingV`, `SwingH` / `setSwingH`); `setSwingV` keeps the byte-0 SwingAuto bit consistent with the chosen value so an auto-mode/position mismatch cannot be expressed. The comfort toggles (sleep / turbo / light / xfan / econo / iFeel / WiFi), the timer, and Fahrenheit mode are documented but have no setter yet — replay a captured frame via RAW to reproduce them.

**Mitsubishi AC field map (decoded logical fields).** Where each control field lives in the 18-byte state. Same status legend. Bytes 0–4 are the fixed signature.

| Field | Location (byte/bit) | Code / range | Status |
|---|---|---|---|
| power | byte 5 bit 5 | on=1 / off=0 | ✅ |
| mode | byte 6 bits 3-5 | heat=1 / dry=2 / cool=3 / auto=4 / fan=7 | ✅ |
| iSee sensor | byte 6 bit 6 | on=1 | 🟡 |
| temperature (integer) | byte 7 bits 0-3 | `°C − 16`, 16–31 °C | ✅ |
| half-degree (0.5 °C) | byte 7 bit 4 | set = +0.5 °C | ✅ |
| wide vane (horizontal) | byte 8 bits 4-7 | 1–5 (L→R) / 6=wide / 8=auto | ✅ |
| fan (airflow) | byte 9 bits 0-2 + bit 7 (auto) | 1–4 (low→max) / 5=quiet; bit 7=auto | ✅ |
| vane (vertical swing) | byte 9 bits 3-5 (+ bit 6 valid) | auto=0 / 1–5 (highest→lowest) / 7=swing | ✅ |
| clock / on / off timer | bytes 10-13 | current / stop / start clock + timer-mode bits | 🟡 |
| ecocool | byte 14 bit 5 | on=1 | 🟡 |
| direct/indirect, i-save | byte 15 | airflow-direction / i-save bits | 🟡 |
| natural flow, vane-left | byte 16 bit 1 / bits 3-5 | dual-vane left side | 🟡 |
| checksum | byte 17 | sum of bytes 0–16 mod 256 | ✅ |

Vane (vertical swing, `Vane` enum with positions `P1`..`P5` after the Panasonic louver convention — Arduino's `HIGH`/`LOW` macros rule out directional names), wide vane (horizontal, `WideVane`), and 0.5 °C (a symmetric `float` `temperatureC()`/`setTemperatureC()` pair, with `halfDegree()` as a convenience reader) all have setters. `setVane` asserts the byte-9 "vane valid" bit; `setMode` rewrites byte 8 and resets the wide vane to MIDDLE, so set the mode first and the wide vane after. The timer/clock block and the comfort/diagnostic bits (iSee, ecocool, direct/indirect, i-save, natural-flow, dual-vane-left) are documented but have no setter yet — the no-setter-for-timers rationale in [DESIGN.md](DESIGN.md) §13 applies the same way; RAW replay reproduces them.

**Fujitsu AC field map (decoded logical fields).** Where each control field lives in the 16-byte long-frame state (ARRAH2E). Same status legend. Bytes 0–7 are fixed/framing: `14 63` (header), byte 2 = device id, `10 10`, byte 5 = `0xFE` long marker, byte 6 = rest-length `0x09`, byte 7 = protocol `0x30`.

| Field | Location (byte/bit) | Code / range | Status |
|---|---|---|---|
| power | frame type | long frame = on / 7-byte short `…02 FD` = off | ✅ |
| temperature (integer) | byte 8 bits 2-7 | `(°C − 16) × 4` (degrees in the high nibble), 16–30 °C | ✅ |
| mode | byte 9 bits 0-2 | auto=0 / cool=1 / dry=2 / fan=3 / heat=4 | ✅ |
| fan (airflow) | byte 10 bits 0-2 | auto=0 / high=1 / med=2 / low=3 / quiet=4 | ✅ |
| swing | byte 10 bits 4-5 | off=0 / vertical=1 / horizontal=2 / both=3 | ✅ |
| Fahrenheit | byte 8 bit 1 | °C / °F unit | 🟡 |
| clean / 10 °C heat | byte 9 bit 3 | on=1 | 🟡 |
| on/off timer | bytes 11-13 | 11-bit minutes + enable bits | 🟡 |
| filter / outside-quiet | byte 14 bits 3, 7 | on=1 | 🟡 |
| checksum (long) | byte 15 | `−(sum of bytes 7…14)` mod 256 | ✅ |
| checksum (short) | byte 6 | `~`byte 5 (inverted command) | ✅ |

Power is the long-vs-short frame selector rather than a state bit (the byte-8 Power bit stays 0 in long frames): `setPower(true)` renders the full 16-byte state, `setPower(false)` renders the short OFF command, whose mode/temp/fan are vendor don't-cares (a decoded OFF frame reports `power=off` and keeps the template defaults for the rest). `temperatureC()`/`setTemperatureC()` clamp to 16–30 °C; the 6-bit Temp field stores `(°C − 16) × 4`, so whole degrees land in the byte-8 high nibble. The Fahrenheit unit, clean/10 °C-heat, timers, and filter/outside-quiet bits are documented but have no setter yet — replay a captured frame via RAW to reproduce them. ARRAH2E is the only implemented model; the other AR-series models are reserved (see the support matrix).

**Daikin classic (ARC433) field map (decoded logical fields).** Where each control field lives in the 35-byte state (three sections: bytes 0–7 / 8–15 / 16–34, each starting with the `11 DA 27` signature). Same status legend.

| Field | Location (byte/bit) | Code / range | Status |
|---|---|---|---|
| power | byte 21 bit 0 | on=1 / off=0 | ✅ |
| mode | byte 21 bits 4-6 | auto=0 / dry=2 / cool=3 / heat=4 / fan=6 | ✅ |
| temperature | byte 22 | `°C × 2`, 10–32 °C (0.5 °C steps) | ✅ |
| fan (airflow) | byte 24 bits 4-7 | auto=A / quiet=B / 3–7 (min→max) | ✅ |
| swing vertical | byte 24 bits 0-3 | on=F / off=0 | ✅ |
| swing horizontal | byte 25 bits 0-3 | on=F / off=0 | ✅ |
| comfort | byte 6 bit 4 | on=1 | 🟡 |
| on/off timer | byte 21 bits 1-2 + bytes 26-28 | enable + 12-bit minutes | 🟡 |
| powerful / quiet | byte 29 bit 0 / bit 5 | on=1 | 🟡 |
| sensor / econo | byte 32 bit 1 / bit 2 | on=1 | 🟡 |
| mold | byte 33 bit 1 | on=1 | 🟡 |
| checksum (×3) | bytes 7 / 15 / 34 | per-section sum mod 256 | ✅ |

The state is rendered as a 5-bit `00000` preamble then the three sections, each with its own `3650/1623 µs` header and ending in a `~29 ms` gap; `toRaw` rewrites the section signatures and all three checksums. `temperatureC()`/`setTemperatureC()` clamp to 10–32 °C and store `°C × 2` (so 0.5 °C steps round-trip). The comfort / timer / powerful / quiet / sensor / econo / mold bits are documented but have no setter yet — replay a captured frame via RAW to reproduce them. This is the only Daikin format implemented; the other sizes are reserved (see the support matrix).

**Toshiba AC field map (decoded logical fields).** Where each control field lives in the 9-byte state. Same status legend. Bytes 0–4 are fixed framing: `F2 0D` (signature + inverted pair), `03 FC` (length/model + inverted pair), `01` (flags).

| Field | Location (byte/bit) | Code / range | Status |
|---|---|---|---|
| power | byte 6 bits 0-2 (Mode) | on = mode ≠ 7 / off = 7 | ✅ |
| mode | byte 6 bits 0-2 | auto=0 / cool=1 / dry=2 / heat=3 / fan=4 | ✅ |
| temperature | byte 5 bits 4-7 | `°C − 17`, 17–30 °C (whole degrees) | ✅ |
| fan (airflow) | byte 6 bits 5-7 | auto=0 / 2–6 (min→max) | ✅ |
| filter | byte 7 bit 4 | on=1 | 🟡 |
| swing | short-message variant | — | ⛔ separate frame |
| checksum | byte 8 | XOR of bytes 0–7 | ✅ |

It is MSB-first; `toRaw` rewrites the fixed framing prefix (signature + inverted pairs + length/flags), recomputes the XOR checksum, and renders the 9-byte message **twice** (separated by the frame gap), matching the standard protocol — third-party decoders rely on the inter-message gap as a bounded footer space. The zero-space is emitted at 440 µs rather than the documented 490 µs: real receivers shift the mark/space boundary and lengthen the received space, and 490 µs would push the recovered zero-space past IRremoteESP8266's tight default ceiling (`490 − kMarkExcess(50) = 440`, ×1.25 ≈ 551 µs); the shorter emit keeps it clear, while our own decoder (30 % tolerance) and normal-bias receivers still recover it. Power is the Mode field's off code (7), so `setPower(false)` writes mode 7. The filter bit has no setter; swing is a separate short message (reserved). This is the only Toshiba format implemented; the 7-byte short and 10-byte long messages are reserved.

**Samsung AC field map (decoded logical fields).** Where each control field lives in the 14-byte state (two 7-byte sections). Same status legend.

| Field | Location (byte/bit) | Code / range | Status |
|---|---|---|---|
| power | byte 6 bits 4-5 + byte 13 bits 4-5 | both `0b11` = on / both `0b00` = off | ✅ |
| mode | byte 12 bits 4-6 | auto=0 / cool=1 / dry=2 / fan=3 / heat=4 | ✅ |
| temperature | byte 11 bits 4-7 | `°C − 16`, 16–30 °C (whole degrees) | ✅ |
| fan (airflow) | byte 12 bits 1-3 | auto=0 / low=2 / med=4 / high=5 / max(turbo)=7 | ✅ |
| swing | byte 9 bits 4-6 | — | 🟡 |
| special fan (Powerful/WindFree/Econo) | byte 10 bits 1-3 | — | 🟡 |
| checksum (section 1) | byte 1 high nibble + byte 2 low nibble | popcount of section bytes 0–6, inverted | ✅ |
| checksum (section 2) | byte 8 high nibble + byte 9 low nibble | popcount of section bytes 7–13, inverted | ✅ |

It is LSB-first; `toRaw` recomputes the two section checksums, then renders the one-time leading header followed by the two sections (each with its own header), as a real remote does. Power is two 2-bit fields written together. Swing and the special fan flags have no setters. This is the only Samsung format implemented; the 21-byte extended (timer) message is reserved.

**Sharp AC field map (decoded logical fields).** Where each control field lives in the 13-byte state. Same status legend. Bytes 0–3 are the fixed header `AA 5A CF 10`.

| Field | Location (byte/bit) | Code / range | Status |
|---|---|---|---|
| power | byte 5 bits 4-7 (PowerSpecial) | on = 3 / off = 2 | ✅ |
| mode | byte 6 bits 0-1 | auto=0 / heat=1 / cool=2 / dry=3 | ✅ |
| temperature | byte 4 bits 0-3 | `°C − 15`, 15–30 °C (Cool/Heat; byte 4 high bits fixed 0xC0; Auto/Dry zero the whole byte) | ✅ |
| fan (airflow) | byte 6 bits 4-6 | auto=2 / med=3 / min=4 / high=5 / max=7 | ✅ |
| model | byte 4 bit 4 + byte 11 bit 4 | A907 (implemented) / A705 / A903 | 🟡 |
| swing | byte 8 bits 0-2 | — | 🟡 |
| ion / clean / timer | bytes 6/7/11 | — | 🟡 |
| special (button) | byte 10 | power = 0x00 (emitted) | ✅ |
| checksum | byte 12 bits 4-7 | nibble-folded XOR of bytes 0–11 + byte 12 low nibble | ✅ |

It is LSB-first; `toRaw` rewrites the fixed header, sets byte 4's high bits to 0xC0 in Cool/Heat (and zeroes the whole temp byte in Auto/Dry, which carry no temperature), sets the Special byte to the "power" value, and recomputes the nibble checksum. Power uses the PowerSpecial field (on = 3 / off = 2). The A705 / A903 models, swing, ion, clean and timer have no setters. This is the only Sharp model implemented; A705 / A903 are reserved.

**Kelvinator field map (decoded logical fields).** Where each control field lives in the 16-byte state (two 8-byte blocks). Same status legend. Byte 3 = `0x50` and byte 11 = `0x70` are fixed markers; bytes 8–10 repeat bytes 0–2.

| Field | Location (byte/bit) | Code / range | Status |
|---|---|---|---|
| power | byte 0 bit 3 | 0/1 | ✅ |
| mode | byte 0 bits 0-2 | auto=0 / cool=1 / dry=2 / fan=3 / heat=4 | ✅ |
| temperature | byte 1 bits 0-3 | `°C − 16`, 16–30 °C (Auto/Dry force 25 °C) | ✅ |
| fan | byte 14 bits 4-6 (+ byte 0 bits 4-5 BasicFan) | auto=0 / 1–5 (BasicFan capped at 3) | ✅ |
| swing (vertical) | byte 0 bit 6 (auto) + byte 4 bits 0-3 (position) | — | 🟡 |
| swing (horizontal) | byte 4 bit 4 | 0/1 | 🟡 |
| turbo / light / ion / X-Fan | byte 2 bits 4-7 | — | 🟡 |
| quiet | byte 12 bit 7 | — | 🟡 |
| checksum | byte 7 bits 4-7 (block 1) / byte 15 bits 4-7 (block 2) | 4-bit block sums | ✅ |

It is LSB-first; `toRaw` forces the fixed markers, mirrors bytes 0–2 into 8–10, recomputes both block checksums, then renders the two blocks (header, 32 bits, `B010` footer, ~20 ms gap, 32 bits, ~40 ms gap). Single format; no model axis. Swing / turbo / quiet / light / ion / X-Fan have no setters.

**Midea field map (decoded logical fields).** Where each control field lives in the 6-byte state. Same status legend. Byte order is transmission order (MSB-first): byte 0 is the fixed Header/Type byte, byte 5 the checksum. Bytes 3–4 default to `0xFF` (timers / sensor off).

| Field | Location (byte/bit) | Code / range | Status |
|---|---|---|---|
| header / type | byte 0 | `0xA1` (Header `0b10100` + command type `0b001`) | ✅ |
| power | byte 1 bit 7 | 0/1 | ✅ |
| mode | byte 1 bits 0-2 | cool=0 / dry=1 / auto=2 / heat=3 / fan=4 | ✅ |
| fan | byte 1 bits 3-4 | auto=0 / low=1 / med=2 / high=3 | ✅ |
| temperature | byte 2 bits 0-4 | `°C − 17`, 17–30 °C (all modes) | ✅ |
| useFahrenheit | byte 2 bit 5 | forced 0 (Celsius) | 🟡 |
| sleep | byte 1 bit 6 | 0/1 | 🟡 |
| on/off timers, sensor/follow-me | bytes 3-4 | — | 🟡 |
| swing / econo / turbo / light / clean / 8 °C-heat / quiet | separate toggle messages | — | ⛔ |
| checksum | byte 5 | negated bit-reversed sum, bit-reversed | ✅ |

It is MSB-first; `toRaw` forces the Header/Type byte and Celsius, writes the checksum, then renders two copies (header, 48 bits, trailer, 5.6 ms gap) with the second copy fully bit-inverted. `fromRaw` requires that inverted second copy to match. Single format; no model axis. Sleep, timers, sensor/follow-me and the toggle messages have no setters.

**Carrier field map (decoded logical fields).** Where each control field lives in the 8-byte state. Same status legend. Bytes 0–1 are the fixed signature `0x84 0x55` (not covered by the checksum); byte 5 is unused.

| Field | Location (byte/bit) | Code / range | Status |
|---|---|---|---|
| signature | bytes 0-1 | fixed `0x84 0x55` | ✅ |
| checksum | byte 2 bits 0-3 | 4-bit nibble sum of bits 20-63 | ✅ |
| mode | byte 2 bits 4-5 | heat=1 / cool=2 / fan=3 | ✅ |
| fan | byte 2 bits 6-7 | auto=0 / low=1 / med=2 / high=3 | ✅ |
| temperature | byte 3 bits 0-3 | `°C − 16`, 16–30 °C (all modes) | ✅ |
| swing (vertical) | byte 3 bit 5 | 0/1 | ✅ |
| power | byte 4 bit 4 | 0/1 | ✅ |
| on/off timers | byte 4 bits 5-6 (enable) + bytes 6-7 | — | 🟡 |
| sleep | byte 4 bit 7 | 0/1 | 🟡 |

It is LSB-first; `toRaw` forces the signature bytes, clears the unused byte, recomputes the nibble checksum, then renders the 8-byte frame once (CARRIER_AC64 is sent once). Single format; no model axis. Sleep and the timers have no setters.

AC types are not send APIs. Sending is always handled by `IRSender::send()`.

### 11.3 Carrier For Long Frames

AC frames are long, and the carrier that delivers them reliably depends on the vendor's timing margins. The library carrier when you do not call `setPhaseAlignedCarrier` is the phase-aligned one (§6.5).

The two carriers trade precision for size. The **phase-aligned** carrier emits each mark as whole carrier-cycle symbols: every mark is an exact integer number of cycles, with no ±1-cycle wobble, but a multi-frame burst expands to several thousand RMT symbols (~17 KB allocated transiently per send) and the larger stream raises the refill-underrun risk under heavy interrupt load. The **hardware** (free-running) carrier uses far fewer symbols, but each mark edge can land up to one carrier period late (~26 µs at 38 kHz).

That wobble matters for tightly-timed vendors:

- **Panasonic** tolerates either carrier — both delivered every frame on the test rig — so the hardware carrier is fine and cheaper.
- **Gree** requires the phase-aligned carrier. Its zero-space (540 µs) is shorter than its bit mark (620 µs), so the hardware carrier's mark wobble pushes spaces out of tolerance and the receiver rejects about half the frames (measured: phase-aligned 50/50 vs hardware ~55%).
- **Mitsubishi** is the same tight-timing case (zero-space 420 µs < bit mark 450 µs) and likewise uses the phase-aligned carrier.
- **Fujitsu** is the same tight-timing case (zero-space 390 µs < bit mark 448 µs), so it uses the phase-aligned carrier by default; the `fujitsu_*` compat studies are intended to confirm the delivery rate on hardware.
- **Daikin** is the most extreme case: its zero-space *equals* its bit mark (both 428 µs), leaving no margin for carrier wobble, so it requires the phase-aligned carrier. Its three-section burst is also long, so the phase-aligned symbol count is the largest of any vendor.
- **Toshiba** has a zero-space (emitted 440 µs — see the framing note) shorter than its bit mark (580 µs), the same tight-margin case as Fujitsu, so it uses the phase-aligned carrier by default.
- **Samsung** has a zero-space (436 µs) shorter than its bit mark (586 µs), so it uses the phase-aligned carrier by default. Its section decoder applies no mark-excess, so the standard 436 µs zero-space clears the third-party window without the shortened emit Toshiba needed.

Recommendation: the phase-aligned carrier is the safe default for AC, and is what you get if you never call `setPhaseAlignedCarrier`. Use the hardware carrier (`setPhaseAlignedCarrier(false)`) only as a memory optimization for loosely-timed vendors such as Panasonic. The carrier affects delivery rate, not byte integrity — a received frame is always byte-correct because it is checksum-validated, and the phase-aligned carrier is not size-limited (durations beyond the 15-bit field are split across symbols).
