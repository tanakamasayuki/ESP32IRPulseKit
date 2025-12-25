#include "Encoder.h"

namespace esp32irpk::codec
{

  bool encodeBitsToRaw(const IRDecodedBits &decoded, IRRawTickBuffer &out_raw)
  {
    (void)decoded;
    (void)out_raw;
    return false;
  }

} // namespace esp32irpk::codec
