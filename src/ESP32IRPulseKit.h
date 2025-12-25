#pragma once

#include <stddef.h>
#include <stdint.h>

namespace esp32irpk
{

  inline constexpr size_t kDefaultMaxDecodeCandidates = 4;

  enum class IRProtocolScheme : uint8_t
  {
    UNKNOWN = 0,
    SPACE_ENC = 1,
  };

  enum class IRProtocolFamily : uint8_t
  {
    UNKNOWN = 0,
    NEC_LIKE = 1,
  };

  enum class IRProtocolID : uint16_t
  {
    UNKNOWN = 0,
    NEC = 1,
    SONY12 = 2,
    SONY15 = 3,
    SONY20 = 4,
    SAMSUNG32 = 5,
    SAMSUNG36 = 6,
    USER1 = 1000,
    USER2 = 1001,
    USER3 = 1002,
    USER4 = 1003,
    USER5 = 1004,
    USER6 = 1005,
    USER7 = 1006,
    USER8 = 1007,
  };

  struct IRPulseUs
  {
    uint32_t mark_us = 0;
    uint32_t space_us = 0;
  };

  struct IRRawTickView
  {
    const uint16_t *ticks = nullptr; // 1 tick = 10us
    size_t len = 0;
  };

  struct IRRawTickBuffer
  {
    uint16_t *ticks = nullptr; // output buffer (1 tick = 10us)
    size_t capacity = 0;
    size_t len = 0;
  };

  enum class IRFrameType : uint8_t
  {
    NORMAL = 0,
    REPEAT = 1,
  };

  struct IRDecodedBits
  {
    IRProtocolID protocol_id = IRProtocolID::UNKNOWN;
    IRFrameType frame_type = IRFrameType::NORMAL;
    uint16_t bit_length = 0;
    uint64_t bits = 0;

    bool isRepeat() const { return frame_type == IRFrameType::REPEAT; }
  };

  enum class IRResultFlags : uint8_t
  {
    NONE = 0,
    DECODE_SKIPPED = 1 << 0,
    RAW_TRUNCATED = 1 << 1,
    RMT_OVERFLOW = 1 << 2,
  };

  inline IRResultFlags operator|(IRResultFlags a, IRResultFlags b)
  {
    return static_cast<IRResultFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
  }

  inline IRResultFlags &operator|=(IRResultFlags &a, IRResultFlags b)
  {
    a = a | b;
    return a;
  }

  struct IRRxStats
  {
    uint32_t queue_overflow_count = 0;
    uint32_t rmt_overflow_count = 0;
    uint32_t raw_truncated_count = 0;
  };

  struct IRProtocolSpec
  {
    IRProtocolID protocol_id = IRProtocolID::UNKNOWN;
    IRProtocolScheme scheme = IRProtocolScheme::UNKNOWN;
    IRProtocolFamily family = IRProtocolFamily::UNKNOWN;

    IRPulseUs header{};
    IRPulseUs one{};
    IRPulseUs zero{};
    IRPulseUs trailer{};

    uint32_t frame_end_gap_us = 0;

    bool lsb_first = true;

    uint16_t bit_length = 0;

    bool has_repeat = false;
    IRPulseUs repeat_header{};
    uint32_t repeat_gap_us = 0;

    uint16_t bit_tol_pct = 25;
    uint16_t endgap_tol_pct = 30;
  };

  struct IRDecodeCandidate
  {
    IRProtocolID protocol_id = IRProtocolID::UNKNOWN;
    int16_t score = 0;
    IRDecodedBits decoded{};
  };

  template <size_t MaxCandidates = esp32irpk::kDefaultMaxDecodeCandidates>
  struct IRReceiveResult
  {
    IRRawTickView raw{};
    IRResultFlags flags = IRResultFlags::NONE;

    uint8_t count = 0; // Max 255 entries due to type width
    IRDecodeCandidate candidates[MaxCandidates]{};

    const esp32irpk::IRDecodeCandidate *candidate() const
    {
      return count > 0 ? &candidates[0] : nullptr;
    }
    const esp32irpk::IRDecodedBits *bits() const
    {
      const auto *c = candidate();
      return c ? &c->decoded : nullptr;
    }
  };

  template <size_t MaxCandidates = esp32irpk::kDefaultMaxDecodeCandidates>
  class IRReceiver
  {
  public:
    explicit IRReceiver(int gpio);
    IRReceiver(int gpio, bool inverted);

    bool setPin(int gpio);
    bool setInverted(bool inverted);

    bool setDecodeCandidates(uint8_t n); // 0..MaxCandidates
    bool setIdleThresholdUs(uint32_t us);

    bool addProtocol(const IRProtocolSpec &spec);
    bool clearProtocols();

    bool begin();
    void end();

    bool read(IRReceiveResult<MaxCandidates> &out);

    bool decode(const IRRawTickView &raw,
                IRReceiveResult<MaxCandidates> &out) const;

    IRRxStats stats() const;
    void resetStats();

  private:
    int gpio_;
    bool inverted_;
    bool begun_ = false;
    uint8_t decode_candidates_ = MaxCandidates;
    uint32_t idle_threshold_us_ = 30000;
    IRRxStats stats_{};
  };

  class IRSender
  {
  public:
    explicit IRSender(int gpio);
    IRSender(int gpio, bool inverted);

    bool setPin(int gpio);
    bool setInverted(bool inverted);

    bool addProtocol(const IRProtocolSpec &spec);
    bool clearProtocols();

    bool begin();
    void end();

    bool send(const esp32irpk::IRRawTickView &raw, uint8_t repeat_count = 0);
    bool send(const esp32irpk::IRRawTickView *raw, uint8_t repeat_count = 0); // nullptr -> false
    bool send(const IRDecodedBits &decoded, uint8_t repeat_count = 0);
    bool send(const IRDecodedBits *decoded, uint8_t repeat_count = 0); // nullptr -> false

    bool encode(const IRDecodedBits &decoded, IRRawTickBuffer &out_raw);

    bool sendNEC(uint16_t address, uint8_t command, bool repeat = false);

  private:
    int gpio_;
    bool inverted_;
    bool begun_ = false;
  };

} // namespace esp32irpk

#include "codec/Receiver.inl"
#include "protocols/NEC.h"
#include "protocols/Sony.h"
#include "protocols/Samsung.h"
