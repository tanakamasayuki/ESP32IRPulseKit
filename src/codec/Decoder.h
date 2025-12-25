#pragma once

#include "../ESP32IRPulseKit.h"

namespace esp32irpk::codec
{

  template <size_t MaxCandidates>
  bool decodeRawToBits(const IRRawTickView &raw, IRReceiveResult<MaxCandidates> &out)
  {
    (void)raw;
    (void)out;
    return false;
  }

} // namespace esp32irpk::codec
