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
- Bit order follows `IRProtocolSpec::lsb_first`.

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
  PANASONIC = 3,
  SONY = 4,
  RC5 = 5,
  RC6 = 6,
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
- `name`: fixed-size copied display name, maximum 15 characters plus the terminating NUL. `addProtocol()` copies `IRProtocolSpec` by value, so the name does not depend on external string lifetime.
- Fixed-length protocols use `bit_length`.
- Variable-length protocols use `min_bit_length..max_bit_length`. A `0` bound falls back to `bit_length`.
- `default_repeat_count` is the default extra send count used when `repeat_count < 0`. `0` means one send; `2` means three sends total.
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
- `DECODE_SKIPPED`: decode was skipped, for example in RAW-only mode.
- `RAW_TRUNCATED`: RAW exceeded the internal limit and was truncated.
- `RMT_OVERFLOW`: RMT receive may have overflowed.

## 3. Built-in Protocols

Built-in protocol specs are available under `esp32irpk::specs`.

```cpp
esp32irpk::specs::NEC
esp32irpk::specs::AEHA
esp32irpk::specs::PANASONIC40
esp32irpk::specs::PANASONIC48
esp32irpk::specs::SONY12
esp32irpk::specs::SONY15
esp32irpk::specs::SONY20
esp32irpk::specs::SAMSUNG32
esp32irpk::specs::SAMSUNG36
esp32irpk::specs::JVC24
esp32irpk::specs::JVC32
esp32irpk::specs::RC5
esp32irpk::specs::RC6_M0_16
esp32irpk::specs::RC6_M6_32
```

`IRProtocolID` is separated not only by waveform timing but also by the logical interpretation of `IRDecodedBits.bits`. For example, Samsung 32-bit and 36-bit use different IDs.

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
- Carrier duty cycle is not public API. The implementation uses an internal fixed duty.

### 6.4 encode

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

## 8. BITS Helpers

Common Frame construction may also be exposed as small helpers under `esp32irpk::bits`. Helpers return `IRDecodedBits`.

```cpp
esp32irpk::IRDecodedBits bits = esp32irpk::bits::nec(0x00ff, 0x34);
tx.send(bits);
```

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
