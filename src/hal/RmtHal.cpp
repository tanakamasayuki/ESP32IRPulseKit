#include "RmtHal.h"
#include "../ESP32IRPulseKit.h"

namespace esp32irpk::hal
{

  bool RmtTx::begin(int gpio, bool inverted)
  {
    gpio_ = gpio;
    inverted_ = inverted;
    begun_ = true;
    return true;
  }

  void RmtTx::end()
  {
    begun_ = false;
  }

  bool RmtTx::send(const esp32irpk::IRRawTickView &raw, uint8_t repeat_count)
  {
    (void)raw;
    (void)repeat_count;
    if (!begun_)
      return false;
    return false;
  }

  bool RmtTx::encode(const esp32irpk::IRDecodedBits &decoded, esp32irpk::IRRawTickBuffer &out_raw)
  {
    (void)decoded;
    (void)out_raw;
    if (!begun_)
      return false;
    return false;
  }

  bool RmtRx::begin(int gpio, bool inverted, uint32_t idle_threshold_us)
  {
    gpio_ = gpio;
    inverted_ = inverted;
    idle_threshold_us_ = idle_threshold_us;
    begun_ = true;
    return true;
  }

  void RmtRx::end()
  {
    begun_ = false;
  }

  bool RmtRx::read(/*out*/ esp32irpk::IRRawTickView &raw)
  {
    (void)raw;
    if (!begun_)
      return false;
    return false;
  }

} // namespace esp32irpk::hal
