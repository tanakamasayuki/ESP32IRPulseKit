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
      (void)in;
      return SonyFrame{};
    }

    esp32irpk::IRDecodedBits toBits() const
    {
      esp32irpk::IRDecodedBits out{};
      return out;
    }
  };

} // namespace esp32irpk::frames
