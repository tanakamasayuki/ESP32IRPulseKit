#pragma once

#include "../ESP32IRPulseKit.h"

namespace esp32irpk::specs
{

  // clang-format off
  inline constexpr IRProtocolSpec SONY12 = {
      .protocol_id      = IRProtocolID::SONY12,
      .scheme           = IRProtocolScheme::SPACE_ENC,
      .family           = IRProtocolFamily::UNKNOWN,
      .header           = {.mark_us = 2400, .space_us =  600},
      .one              = {.mark_us = 1200, .space_us =  600},
      .zero             = {.mark_us =  600, .space_us =  600},
      .trailer          = {.mark_us =    0, .space_us =    0},
      .frame_end_gap_us = 45000,
      .lsb_first        = true,
      .bit_length       = 12,
  };

  inline constexpr IRProtocolSpec SONY15 = {
      .protocol_id      = IRProtocolID::SONY15,
      .scheme           = IRProtocolScheme::SPACE_ENC,
      .family           = IRProtocolFamily::UNKNOWN,
      .header           = {.mark_us = 2400, .space_us =  600},
      .one              = {.mark_us = 1200, .space_us =  600},
      .zero             = {.mark_us =  600, .space_us =  600},
      .trailer          = {.mark_us =    0, .space_us =    0},
      .frame_end_gap_us = 45000,
      .lsb_first        = true,
      .bit_length       = 15,
  };

  inline constexpr IRProtocolSpec SONY20 = {
      .protocol_id      = IRProtocolID::SONY20,
      .scheme           = IRProtocolScheme::SPACE_ENC,
      .family           = IRProtocolFamily::UNKNOWN,
      .header           = {.mark_us = 2400, .space_us =  600},
      .one              = {.mark_us = 1200, .space_us =  600},
      .zero             = {.mark_us =  600, .space_us =  600},
      .trailer          = {.mark_us =    0, .space_us =    0},
      .frame_end_gap_us = 45000,
      .lsb_first        = true,
      .bit_length       = 20,
  };
  // clang-format on

} // namespace esp32irpk::specs

namespace esp32irpk::frames
{

  struct SonyFrame
  {
    uint32_t data = 0;
    bool is_repeat = false;

    static SonyFrame fromBits(const esp32irpk::IRDecodedBits &in)
    {
      SonyFrame out{};
      if (in.frame_type == esp32irpk::IRFrameType::REPEAT)
      {
        out.is_repeat = true;
        return out;
      }

      if (in.bit_length > 0 && in.bit_length < 32)
      {
        uint32_t mask = static_cast<uint32_t>((1ULL << in.bit_length) - 1ULL);
        out.data = static_cast<uint32_t>(in.bits & mask);
      }
      else
      {
        out.data = static_cast<uint32_t>(in.bits);
      }
      return out;
    }

    esp32irpk::IRDecodedBits toBits() const
    {
      esp32irpk::IRDecodedBits out{};
      uint16_t bit_length = 12;
      esp32irpk::IRProtocolID pid = esp32irpk::IRProtocolID::SONY12;
      if (data > 0x0FFFu)
      {
        bit_length = 15;
        pid = esp32irpk::IRProtocolID::SONY15;
      }
      if (data > 0x7FFFu)
      {
        bit_length = 20;
        pid = esp32irpk::IRProtocolID::SONY20;
      }

      out.protocol_id = pid;
      if (is_repeat)
      {
        out.frame_type = esp32irpk::IRFrameType::REPEAT;
        out.bit_length = 0;
        out.bits = 0xFFFFFFFFFFFFFFFFULL;
        return out;
      }

      uint64_t mask = (bit_length == 64) ? 0xFFFFFFFFFFFFFFFFULL : ((1ULL << bit_length) - 1ULL);
      out.frame_type = esp32irpk::IRFrameType::NORMAL;
      out.bit_length = bit_length;
      out.bits = static_cast<uint64_t>(data) & mask;
      return out;
    }
  };

} // namespace esp32irpk::frames
