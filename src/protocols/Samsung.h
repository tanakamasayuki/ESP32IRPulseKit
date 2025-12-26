#pragma once

#include "../ESP32IRPulseKit.h"

namespace esp32irpk::specs
{

  // clang-format off
  inline constexpr IRProtocolSpec SAMSUNG32 = {
      .protocol_id      = IRProtocolID::SAMSUNG32,
      .scheme           = IRProtocolScheme::SPACE_ENC,
      .family           = IRProtocolFamily::NEC_LIKE,
      .header           = {.mark_us = 4500, .space_us = 4500},
      .one              = {.mark_us =  560, .space_us = 1690},
      .zero             = {.mark_us =  560, .space_us =  560},
      .trailer          = {.mark_us =  560, .space_us =    0},
      .frame_end_gap_us = 30000,
      .lsb_first        = true,
      .bit_length       = 32,
  };

  inline constexpr IRProtocolSpec SAMSUNG36 = {
      .protocol_id      = IRProtocolID::SAMSUNG36,
      .scheme           = IRProtocolScheme::SPACE_ENC,
      .family           = IRProtocolFamily::NEC_LIKE,
      .header           = {.mark_us = 4500, .space_us = 4500},
      .one              = {.mark_us =  560, .space_us = 1690},
      .zero             = {.mark_us =  560, .space_us =  560},
      .trailer          = {.mark_us =  560, .space_us =    0},
      .frame_end_gap_us = 30000,
      .lsb_first        = true,
      .bit_length       = 36,
  };
  // clang-format on

} // namespace esp32irpk::specs

namespace esp32irpk::frames
{

  struct SamsungFrame
  {
    uint32_t address = 0;
    uint32_t command = 0;
    bool is_repeat = false;

    static SamsungFrame fromBits(const esp32irpk::IRDecodedBits &in)
    {
      SamsungFrame out{};
      if (in.frame_type == esp32irpk::IRFrameType::REPEAT)
      {
        out.is_repeat = true;
        return out;
      }

      uint64_t bits = in.bits;
      out.address = static_cast<uint32_t>(bits & 0xFFFFULL);
      if (in.protocol_id == esp32irpk::IRProtocolID::SAMSUNG36 || in.bit_length == 36)
      {
        out.command = static_cast<uint32_t>((bits >> 16) & 0xFFFFFULL);
      }
      else
      {
        out.command = static_cast<uint32_t>((bits >> 16) & 0xFFFFULL);
      }
      return out;
    }

    esp32irpk::IRDecodedBits toBits() const
    {
      esp32irpk::IRDecodedBits out{};
      bool use36 = command > 0xFFFFu;
      out.protocol_id = use36 ? esp32irpk::IRProtocolID::SAMSUNG36
                              : esp32irpk::IRProtocolID::SAMSUNG32;
      if (is_repeat)
      {
        out.frame_type = esp32irpk::IRFrameType::REPEAT;
        out.bit_length = 0;
        out.bits = 0xFFFFFFFFFFFFFFFFULL;
        return out;
      }

      uint64_t bits = 0;
      bits |= static_cast<uint64_t>(address & 0xFFFFu);
      bits |= (static_cast<uint64_t>(command & (use36 ? 0xFFFFFu : 0xFFFFu)) << 16);

      out.frame_type = esp32irpk::IRFrameType::NORMAL;
      out.bit_length = use36 ? 36 : 32;
      out.bits = bits;
      return out;
    }
  };

} // namespace esp32irpk::frames
