#pragma once

#include <stddef.h>
#include <stdint.h>

namespace esp32irpk
{
  struct IRRawTickView;
  struct IRRawTickBuffer;
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
  };

  class RmtRx
  {
  public:
    bool begin(int gpio, bool inverted, uint32_t idle_threshold_us);
    void end();
    bool read(/*out*/ esp32irpk::IRRawTickView &raw);

  private:
    int gpio_ = -1;
    bool inverted_ = false;
    bool begun_ = false;
    uint32_t idle_threshold_us_ = 30000;
  };

} // namespace esp32irpk::hal
