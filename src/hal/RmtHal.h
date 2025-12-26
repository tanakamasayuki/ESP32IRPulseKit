#pragma once

#include <stddef.h>
#include <stdint.h>
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

  // Stub HAL for RMT TX/RX. Actual implementation should map to ESP-IDF RMT driver.
  class RmtTx
  {
  public:
    bool begin(int gpio, bool inverted);
    void end();
    bool send(const esp32irpk::IRRawTickView &raw, uint8_t repeat_count);
    bool encode(const esp32irpk::IRDecodedBits &decoded, esp32irpk::IRRawTickBuffer &out_raw);

  private:
    int gpio_ = -1;
    bool inverted_ = false;
    bool begun_ = false;
    void *tx_channel_ = nullptr;
    void *tx_encoder_ = nullptr;
  };

  class RmtRx
  {
  public:
    bool begin(int gpio, bool inverted, uint32_t idle_threshold_us);
    void end();
    bool read(/*out*/ esp32irpk::IRRawTickView &raw);

  private:
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
    std::vector<uint16_t> ticks_buf_;
  };

} // namespace esp32irpk::hal
