#pragma once

#include "../ESP32IRPulseKit.h"

namespace esp32irpk::codec
{

    bool encodeBitsToRaw(const IRDecodedBits &decoded, IRRawTickBuffer &out_raw);

} // namespace esp32irpk::codec
