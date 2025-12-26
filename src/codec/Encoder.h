#pragma once

#include "../ESP32IRPulseKit.h"

namespace esp32irpk::codec
{

    bool encodeBitsToRaw(const IRDecodedBits &decoded,
                         const IRProtocolSpec *specs,
                         size_t spec_count,
                         IRRawTickBuffer &out_raw);

} // namespace esp32irpk::codec
