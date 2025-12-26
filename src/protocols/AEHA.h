#pragma once

#include "../ESP32IRPulseKit.h"

namespace esp32irpk::specs
{

  // clang-format off
  inline constexpr IRProtocolSpec AEHA = {
      .protocol_id      = IRProtocolID::AEHA,
      .name             = "AEHA",
      .scheme           = IRProtocolScheme::SPACE_ENC,
      .family           = IRProtocolFamily::NEC_LIKE,
      .header           = {.mark_us = 3400, .space_us = 1700},
      .one              = {.mark_us =  425, .space_us = 1275},
      .zero             = {.mark_us =  425, .space_us =  425},
      .trailer          = {.mark_us =  425, .space_us =    0},
      .frame_end_gap_us = 10800,
      .lsb_first        = true,
      .bit_length       = 64,   // nominal upper bound
      .min_bit_length   = 48,
      .max_bit_length   = 64,
      .bit_tol_pct      = 30,
      .endgap_tol_pct   = 35,
  };

  inline constexpr IRProtocolSpec PANASONIC40 = {
      .protocol_id      = IRProtocolID::PANASONIC40,
      .name             = "PANASONIC40",
      .scheme           = IRProtocolScheme::SPACE_ENC,
      .family           = IRProtocolFamily::NEC_LIKE,
      .header           = {.mark_us = 3500, .space_us = 1750},
      .one              = {.mark_us =  430, .space_us = 1300},
      .zero             = {.mark_us =  430, .space_us =  430},
      .trailer          = {.mark_us =  430, .space_us =    0},
      .frame_end_gap_us = 40000,
      .lsb_first        = true,
      .bit_length       = 40,
      .bit_tol_pct      = 30,
      .endgap_tol_pct   = 35,
  };

  inline constexpr IRProtocolSpec PANASONIC48 = {
      .protocol_id      = IRProtocolID::PANASONIC48,
      .name             = "PANASONIC48",
      .scheme           = IRProtocolScheme::SPACE_ENC,
      .family           = IRProtocolFamily::NEC_LIKE,
      .header           = {.mark_us = 3500, .space_us = 1750},
      .one              = {.mark_us =  430, .space_us = 1300},
      .zero             = {.mark_us =  430, .space_us =  430},
      .trailer          = {.mark_us =  430, .space_us =    0},
      .frame_end_gap_us = 40000,
      .lsb_first        = true,
      .bit_length       = 48,
      .bit_tol_pct      = 30,
      .endgap_tol_pct   = 35,
  };
  // clang-format on

} // namespace esp32irpk::specs

namespace esp32irpk::frames
{

  struct AEHAFrame
  {
    uint64_t data = 0;
    uint16_t bit_length = 0;
    bool is_repeat = false;

    static AEHAFrame fromBits(const esp32irpk::IRDecodedBits &in)
    {
      AEHAFrame out{};
      if (in.frame_type == esp32irpk::IRFrameType::REPEAT)
      {
        out.is_repeat = true;
        return out;
      }
      out.data = in.bits;
      out.bit_length = in.bit_length;
      return out;
    }

    esp32irpk::IRDecodedBits toBits() const
    {
      esp32irpk::IRDecodedBits out{};
      out.protocol_id = esp32irpk::IRProtocolID::AEHA;
      if (is_repeat)
      {
        out.frame_type = esp32irpk::IRFrameType::REPEAT;
        out.bit_length = 0;
        out.bits = 0xFFFFFFFFFFFFFFFFULL;
        return out;
      }
      out.frame_type = esp32irpk::IRFrameType::NORMAL;
      out.bit_length = bit_length;
      out.bits = data;
      return out;
    }
  };

  struct Panasonic40Frame
  {
    uint64_t data = 0;
    bool is_repeat = false;

    static Panasonic40Frame fromBits(const esp32irpk::IRDecodedBits &in)
    {
      Panasonic40Frame out{};
      if (in.frame_type == esp32irpk::IRFrameType::REPEAT)
      {
        out.is_repeat = true;
        return out;
      }
      out.data = in.bits;
      return out;
    }

    esp32irpk::IRDecodedBits toBits() const
    {
      esp32irpk::IRDecodedBits out{};
      out.protocol_id = esp32irpk::IRProtocolID::PANASONIC40;
      if (is_repeat)
      {
        out.frame_type = esp32irpk::IRFrameType::REPEAT;
        out.bit_length = 0;
        out.bits = 0xFFFFFFFFFFFFFFFFULL;
        return out;
      }
      out.frame_type = esp32irpk::IRFrameType::NORMAL;
      out.bit_length = 40;
      out.bits = data;
      return out;
    }
  };

  struct Panasonic48Frame
  {
    uint64_t data = 0;
    bool is_repeat = false;

    static Panasonic48Frame fromBits(const esp32irpk::IRDecodedBits &in)
    {
      Panasonic48Frame out{};
      if (in.frame_type == esp32irpk::IRFrameType::REPEAT)
      {
        out.is_repeat = true;
        return out;
      }
      out.data = in.bits;
      return out;
    }

    esp32irpk::IRDecodedBits toBits() const
    {
      esp32irpk::IRDecodedBits out{};
      out.protocol_id = esp32irpk::IRProtocolID::PANASONIC48;
      if (is_repeat)
      {
        out.frame_type = esp32irpk::IRFrameType::REPEAT;
        out.bit_length = 0;
        out.bits = 0xFFFFFFFFFFFFFFFFULL;
        return out;
      }
      out.frame_type = esp32irpk::IRFrameType::NORMAL;
      out.bit_length = 48;
      out.bits = data;
      return out;
    }
  };

} // namespace esp32irpk::frames
