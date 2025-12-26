#pragma once

#include "../ESP32IRPulseKit.h"

namespace esp32irpk::specs
{
  // https://ww1.microchip.com/downloads/jp/AppNotes/01064A.pdf

  // clang-format off
  inline constexpr IRProtocolSpec SONY12 = {
      .protocol_id      = IRProtocolID::SONY12,
      .name             = "SONY12",
      .scheme           = IRProtocolScheme::SPACE_ENC,
      .family           = IRProtocolFamily::UNKNOWN,
      .header           = {.mark_us = 2400, .space_us =  600},
      .one              = {.mark_us = 1200, .space_us =  600},
      .zero             = {.mark_us =  600, .space_us =  600},
      .trailer          = {.mark_us =    0, .space_us =    0},
      .frame_end_gap_us = 24000, // 45000 - (2400 + 600) - (900 + 600)*12
      .lsb_first        = true,
      .bit_length       = 12,
      .bit_tol_pct      = 55,
  };

  inline constexpr IRProtocolSpec SONY15 = {
      .protocol_id      = IRProtocolID::SONY15,
      .name             = "SONY15",
      .scheme           = IRProtocolScheme::SPACE_ENC,
      .family           = IRProtocolFamily::UNKNOWN,
      .header           = {.mark_us = 2400, .space_us =  600},
      .one              = {.mark_us = 1200, .space_us =  600},
      .zero             = {.mark_us =  600, .space_us =  600},
      .trailer          = {.mark_us =    0, .space_us =    0},
      .frame_end_gap_us = 19500, // 45000 - (2400 + 600) - (900 + 600)*15
      .lsb_first        = true,
      .bit_length       = 15,
      .bit_tol_pct      = 55,
  };

  inline constexpr IRProtocolSpec SONY20 = {
      .protocol_id      = IRProtocolID::SONY20,
      .name             = "SONY20",
      .scheme           = IRProtocolScheme::SPACE_ENC,
      .family           = IRProtocolFamily::UNKNOWN,
      .header           = {.mark_us = 2400, .space_us =  600},
      .one              = {.mark_us = 1200, .space_us =  600},
      .zero             = {.mark_us =  600, .space_us =  600},
      .trailer          = {.mark_us =    0, .space_us =    0},
      .frame_end_gap_us = 12000, // 45000 - (2400 + 600) - (900 + 600)*20
      .lsb_first        = true,
      .bit_length       = 20,
      .bit_tol_pct      = 55,
  };
  // clang-format on

} // namespace esp32irpk::specs

namespace esp32irpk::frames
{

  struct Sony12Frame
  {
    uint32_t data = 0;
    bool is_repeat = false;

    static Sony12Frame fromBits(const esp32irpk::IRDecodedBits &in)
    {
      Sony12Frame out{};
      if (in.frame_type == esp32irpk::IRFrameType::REPEAT)
      {
        out.is_repeat = true;
        return out;
      }

      uint32_t mask = (1U << 12) - 1U;
      out.data = static_cast<uint32_t>(in.bits) & mask;
      return out;
    }

    esp32irpk::IRDecodedBits toBits() const
    {
      esp32irpk::IRDecodedBits out{};
      out.protocol_id = esp32irpk::IRProtocolID::SONY12;
      if (is_repeat)
      {
        out.frame_type = esp32irpk::IRFrameType::REPEAT;
        out.bit_length = 0;
        out.bits = 0xFFFFFFFFFFFFFFFFULL;
        return out;
      }

      uint32_t mask = (1U << 12) - 1U;
      out.frame_type = esp32irpk::IRFrameType::NORMAL;
      out.bit_length = 12;
      out.bits = static_cast<uint64_t>(data & mask);
      return out;
    }
  };

  struct Sony15Frame
  {
    uint32_t data = 0;
    bool is_repeat = false;

    static Sony15Frame fromBits(const esp32irpk::IRDecodedBits &in)
    {
      Sony15Frame out{};
      if (in.frame_type == esp32irpk::IRFrameType::REPEAT)
      {
        out.is_repeat = true;
        return out;
      }

      uint32_t mask = (1U << 15) - 1U;
      out.data = static_cast<uint32_t>(in.bits) & mask;
      return out;
    }

    esp32irpk::IRDecodedBits toBits() const
    {
      esp32irpk::IRDecodedBits out{};
      out.protocol_id = esp32irpk::IRProtocolID::SONY15;
      if (is_repeat)
      {
        out.frame_type = esp32irpk::IRFrameType::REPEAT;
        out.bit_length = 0;
        out.bits = 0xFFFFFFFFFFFFFFFFULL;
        return out;
      }

      uint32_t mask = (1U << 15) - 1U;
      out.frame_type = esp32irpk::IRFrameType::NORMAL;
      out.bit_length = 15;
      out.bits = static_cast<uint64_t>(data & mask);
      return out;
    }
  };

  struct Sony20Frame
  {
    uint32_t data = 0;
    bool is_repeat = false;

    static Sony20Frame fromBits(const esp32irpk::IRDecodedBits &in)
    {
      Sony20Frame out{};
      if (in.frame_type == esp32irpk::IRFrameType::REPEAT)
      {
        out.is_repeat = true;
        return out;
      }

      uint32_t mask = (1U << 20) - 1U;
      out.data = static_cast<uint32_t>(in.bits) & mask;
      return out;
    }

    esp32irpk::IRDecodedBits toBits() const
    {
      esp32irpk::IRDecodedBits out{};
      out.protocol_id = esp32irpk::IRProtocolID::SONY20;
      if (is_repeat)
      {
        out.frame_type = esp32irpk::IRFrameType::REPEAT;
        out.bit_length = 0;
        out.bits = 0xFFFFFFFFFFFFFFFFULL;
        return out;
      }

      uint32_t mask = (1U << 20) - 1U;
      out.frame_type = esp32irpk::IRFrameType::NORMAL;
      out.bit_length = 20;
      out.bits = static_cast<uint64_t>(data & mask);
      return out;
    }
  };

} // namespace esp32irpk::frames
