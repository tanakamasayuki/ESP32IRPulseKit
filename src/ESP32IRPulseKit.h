#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include <algorithm>
#include <vector>

#include "hal/RmtHal.h"

namespace esp32irpk
{

  inline constexpr size_t kDefaultMaxDecodeCandidates = 4;
  inline constexpr uint32_t kDefaultCarrierHz = 38000;
  inline constexpr float kDefaultCarrierDuty = 0.33f; // carrier on-time fraction

  enum class IRProtocolScheme : uint8_t
  {
    UNKNOWN = 0,
    SPACE_ENC = 1,
    BIPHASE = 2,
  };

  enum class IRProtocolFamily : uint8_t
  {
    UNKNOWN = 0,
    NEC_LIKE = 1,
    AEHA = 2,
    SONY = 3,
    RC5 = 4,
    RC6 = 5,
  };

  enum class IRProtocolID : uint16_t
  {
    UNKNOWN = 0,
    NEC = 1,
    AEHA = 2,
    SONY12 = 3,
    SONY15 = 4,
    SONY20 = 5,
    SAMSUNG32 = 6,
    SAMSUNG36 = 7,
    JVC = 8,
    RC5 = 9,
    RC6_M0_16 = 10,
    RC6_M6_32 = 11,
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
    uint32_t mark_us = 0;  // high duration in microseconds
    uint32_t space_us = 0; // low duration in microseconds
  };

  struct IRRawTickView
  {
    const uint16_t *ticks = nullptr; // pointer to tick array (1 tick = 10us)
    size_t len = 0;                  // number of tick entries
  };

  struct IRRawTickBuffer
  {
    uint16_t *ticks = nullptr; // output buffer (1 tick = 10us)
    size_t capacity = 0;       // total allocated entries
    size_t len = 0;            // number of valid entries
  };

  enum class IRFrameType : uint8_t
  {
    NORMAL = 0,
    REPEAT = 1,
  };

  struct IRDecodedBits
  {
    IRProtocolID protocol_id = IRProtocolID::UNKNOWN; // detected protocol
    IRFrameType frame_type = IRFrameType::NORMAL;     // NORMAL or REPEAT
    uint16_t bit_length = 0;                          // bits length (0..64)
    uint64_t bits = 0;                                // packed bits (LSB/MSB depends on spec)

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
    uint32_t queue_overflow_count = 0; // dropped entries due to queue full
    uint32_t rmt_overflow_count = 0;   // RMT hardware overflow occurrences
    uint32_t raw_truncated_count = 0;  // times raw data was truncated
  };

  struct IRProtocolSpec
  {
    IRProtocolID protocol_id = IRProtocolID::UNKNOWN;    // unique protocol id
    char name[16] = {};                                  // optional display name (max 15 chars + null)
    IRProtocolScheme scheme = IRProtocolScheme::UNKNOWN; // encoding scheme
    IRProtocolFamily family = IRProtocolFamily::UNKNOWN; // protocol family for shared rules

    IRPulseUs header{};  // leading mark/space
    IRPulseUs one{};     // bit-1 mark/space
    IRPulseUs zero{};    // bit-0 mark/space
    IRPulseUs trailer{}; // trailing mark/space

    uint32_t gap_threshold_us = 0;  // minimum gap to split frames; 0 disables gap-based split
    uint32_t idle_threshold_us = 0; // preferred RMT idle threshold; 0 = use receiver setting
    uint32_t carrier_hz = 0;        // preferred TX carrier; 0 = library default

    bool lsb_first = true; // bit order

    uint16_t bit_length = 0;     // default bit length (or fixed length)
    uint16_t min_bit_length = 0; // lower bound for variable-length protocols
    uint16_t max_bit_length = 0; // upper bound for variable-length protocols

    bool has_repeat = false;         // protocol defines repeat frame
    IRPulseUs repeat_header{};       // repeat frame header
    uint32_t repeat_gap_us = 0;      // frame start-to-start gap expectation
    int8_t default_repeat_count = 0; // default repeats when send(..., repeat_count < 0)

    uint16_t bit_tol_pct = 25; // tolerance percentage for pulse matching

    uint8_t order = 0; // registration order (tie-breaker)
  };

  struct IRDecodeCandidate
  {
    IRProtocolID protocol_id = IRProtocolID::UNKNOWN; // detected protocol id
    char name[16] = {};                               // display name copy from spec
    uint8_t order = 0;                                // spec registration order
    int16_t score = 0;                                // final score after adjustments
    size_t consumed_len = 0;                          // ticks consumed for this decode
    IRDecodedBits decoded{};                          // decoded bit payload
  };

  template <size_t MaxCandidates = esp32irpk::kDefaultMaxDecodeCandidates>
  struct IRReceiveResult
  {
    IRRawTickView raw{};                       // raw ticks used/returned
    IRResultFlags flags = IRResultFlags::NONE; // status flags

    uint8_t count = 0;                             // candidate count (<= MaxCandidates)
    IRDecodeCandidate candidates[MaxCandidates]{}; // sorted by score

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
    bool setScoreThreshold(int16_t score); // min score to accept candidates

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
    int gpio_;                                  // assigned GPIO number
    bool inverted_;                             // true if input signal is inverted
    bool begun_ = false;                        // begin() was called
    uint8_t order_counter_ = 0;                 // running order for protocol registration
    uint8_t decode_candidates_ = MaxCandidates; // max candidates to keep
    uint32_t idle_threshold_us_ = 30000;        // current idle threshold setting
    int16_t score_threshold_ = 0;               // min score; candidates below are discarded
    IRRxStats stats_{};                         // runtime RX statistics
    std::vector<IRProtocolSpec> protocols_{};   // registered protocol list
    hal::RmtRx rmt_rx_{};                       // HAL receiver instance
  };

  class IRSender
  {
  public:
    explicit IRSender(int gpio);
    IRSender(int gpio, bool inverted);

    bool setPin(int gpio);
    bool setInverted(bool inverted);
    bool setCarrierHz(uint32_t hz);
    bool clearCarrierHz();
    bool disableCarrier();         // send with no carrier modulation (solid marks)
    bool setCarrierDuty(float duty); // carrier on-time fraction (0 < duty < 1)
    // Carrier generation method (see DESIGN §12). The default is phase-aligned,
    // symbol-encoded carrier; pass false to fall back to the free-running
    // hardware carrier (lower symbol throughput, but with ±1-cycle mark jitter).
    // Must be called before begin(); returns false once begun.
    bool setPhaseAlignedCarrier(bool enable);
    // Number of RMT memory blocks for the TX channel (1 block =
    // SOC_RMT_MEM_WORDS_PER_CHANNEL symbols). Larger values give the
    // phase-aligned path more refill headroom but consume the shared RMT TX
    // memory pool, leaving fewer blocks for other channels. 0 = library default.
    // Must be called before begin().
    bool setTxMemBlocks(uint8_t blocks);

    bool addProtocol(const IRProtocolSpec &spec);
    bool clearProtocols();

    bool begin();
    void end();

    bool send(const esp32irpk::IRRawTickView &raw, int8_t repeat_count = -1);
    bool send(const esp32irpk::IRRawTickView *raw, int8_t repeat_count = -1); // nullptr -> false
    bool send(const IRDecodedBits &decoded, int8_t repeat_count = -1);
    bool send(const IRDecodedBits *decoded, int8_t repeat_count = -1); // nullptr -> false

    bool encode(const IRDecodedBits &decoded, IRRawTickBuffer &out_raw);

  private:
    int gpio_;                                             // assigned GPIO number
    bool inverted_;                                        // true if output should be inverted
    bool begun_ = false;                                   // begin() was called
    bool carrier_override_ = false;                        // true when carrier_hz_ is explicitly set
    uint32_t carrier_hz_ = kDefaultCarrierHz;               // explicit/default carrier frequency
    float carrier_duty_ = kDefaultCarrierDuty;              // carrier on-time fraction
    uint8_t order_counter_ = 0;                            // running order for protocol registration
    std::vector<IRProtocolSpec> protocols_{};              // registered protocol list
    hal::RmtTx rmt_tx_{};                                  // HAL transmitter instance
    static constexpr size_t kMaxEncodedTicks = 2 * 64 + 6; // internal buffer size for encoding
    uint16_t encode_buf_[kMaxEncodedTicks]{};              // scratch buffer for encoded ticks
  };

} // namespace esp32irpk

#include "protocols/NEC.h"
#include "protocols/Sony.h"
#include "protocols/Samsung.h"
#include "protocols/JVC.h"
#include "protocols/AEHA.h"
#include "protocols/RC.h"

namespace esp32irpk::detail
{
  inline void addDefaultProtocols(std::vector<IRProtocolSpec> &out)
  {
    auto has = [&](IRProtocolID id)
    {
      return std::any_of(out.begin(), out.end(),
                         [&](const IRProtocolSpec &spec)
                         { return spec.protocol_id == id; });
    };
    if (!has(specs::NEC.protocol_id))
      out.push_back(specs::NEC);
    if (!has(specs::AEHA.protocol_id))
      out.push_back(specs::AEHA);
    if (!has(specs::SONY12.protocol_id))
      out.push_back(specs::SONY12);
    if (!has(specs::SONY15.protocol_id))
      out.push_back(specs::SONY15);
    if (!has(specs::SONY20.protocol_id))
      out.push_back(specs::SONY20);
    if (!has(specs::SAMSUNG32.protocol_id))
      out.push_back(specs::SAMSUNG32);
    if (!has(specs::SAMSUNG36.protocol_id))
      out.push_back(specs::SAMSUNG36);
    if (!has(specs::JVC.protocol_id))
      out.push_back(specs::JVC);
    if (!has(specs::RC5.protocol_id))
      out.push_back(specs::RC5);
    if (!has(specs::RC6_M0_16.protocol_id))
      out.push_back(specs::RC6_M0_16);
    if (!has(specs::RC6_M6_32.protocol_id))
      out.push_back(specs::RC6_M6_32);
  }
} // namespace esp32irpk::detail

#include "codec/Receiver.inl"
