#pragma once

#include <stddef.h>
#include <stdint.h>
#include <deque>
#include <vector>

#if defined(ESP_PLATFORM)
#include <driver/rmt_rx.h>
#endif

namespace esp32irpk
{
  struct IRRawTickView;
  struct IRRawTickBuffer;
  struct IRDecodedBits;
}

namespace esp32irpk::hal
{

  // Internal RMT TX/RX wrapper. Public encoding/decoding stays in codec/.
  class RmtTx
  {
  public:
    bool begin(int gpio, bool inverted);
    void end();
    bool applyCarrierHz(uint32_t carrier_hz);
    bool send(const esp32irpk::IRRawTickView &raw, int8_t repeat_count, uint32_t carrier_hz);
    bool isSending() const { return sending_; }

  private:
    int gpio_ = -1;
    bool inverted_ = false;
    bool begun_ = false;
    bool sending_ = false;
    uint32_t carrier_hz_ = 0;
    void *tx_channel_ = nullptr;
    void *tx_encoder_ = nullptr;
  };

  class RmtRx
  {
  public:
    bool begin(int gpio, bool inverted, uint32_t idle_threshold_us);
    void end();
    bool read(/*out*/ esp32irpk::IRRawTickView &raw);
    bool consumeTruncatedFlag();
    bool consumeOverflowFlag();
    void consume(size_t ticks);
    uint32_t consumeQueueOverflowCount();

  private:
    struct RxFrame
    {
      std::vector<uint16_t> ticks;
      size_t start = 0;
      bool truncated = false;
      bool overflow = false;
    };

    void drainRxQueue();
    bool hasCurrent() const;
    bool loadNextFrame();
    bool startReceive();
#if defined(ESP_PLATFORM)
    bool startReceiveInternal();
    bool handleRecvDone(const rmt_rx_done_event_data_t *edata);
#endif

    int gpio_ = -1;
    bool inverted_ = false;
    bool begun_ = false;
    uint32_t idle_threshold_us_ = 30000;
    void *rx_channel_ = nullptr;
    void *rx_sem_ = nullptr;
    std::vector<uint32_t> sym_buf_;
    size_t sym_count_ = 0;
    bool has_frame_ = false;
    bool receiving_ = false;
    bool last_truncated_ = false;
    bool last_overflow_ = false;
    std::deque<RxFrame> queue_;
    RxFrame current_;
    uint32_t queue_overflow_count_ = 0;
  };

} // namespace esp32irpk::hal
