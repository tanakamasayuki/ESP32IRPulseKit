#pragma once

#include "../ESP32IRPulseKit.h"

namespace esp32irpk::specs
{
  // https://support.jvc.com/consumer/support/documents/RemoteCodes.pdf

  // clang-format off
  inline constexpr IRProtocolSpec JVC24 = {
      .protocol_id      = IRProtocolID::JVC24,
      .name             = "JVC24",
      .scheme           = IRProtocolScheme::SPACE_ENC,
      .family           = IRProtocolFamily::NEC_LIKE,
      .header           = {.mark_us = 8440, .space_us = 4220},
      .one              = {.mark_us =  527, .space_us = 1583}, // widen to match observed devices
      .zero             = {.mark_us =  527, .space_us =  527},
      .trailer          = {.mark_us =  527, .space_us =    0},
      .gap_threshold_us = 45000,
      .idle_threshold_us= 63000,
      .carrier_hz       = kDefaultCarrierHz,
      .lsb_first        = true,
      .bit_length       = 24,
      .default_repeat_count = 0,
      .bit_tol_pct      = 30,
  };
  // clang-format on

  // clang-format off
  inline constexpr IRProtocolSpec JVC32 = {
      .protocol_id      = IRProtocolID::JVC32,
      .name             = "JVC32",
      .scheme           = IRProtocolScheme::SPACE_ENC,
      .family           = IRProtocolFamily::NEC_LIKE,
      .header           = {.mark_us = 8400, .space_us = 4200},
      .one              = {.mark_us =  550, .space_us = 1800}, // widen to match observed devices
      .zero             = {.mark_us =  550, .space_us =  550},
      .trailer          = {.mark_us =  550, .space_us =    0},
      .gap_threshold_us = 45000,
      .idle_threshold_us= 63000,
      .carrier_hz       = kDefaultCarrierHz,
      .lsb_first        = true,
      .bit_length       = 32,
      .default_repeat_count = 0,
      .bit_tol_pct      = 30,
  };
  // clang-format on

} // namespace esp32irpk::specs

namespace esp32irpk::frames
{

  struct JVC24Frame
  {
    uint32_t data = 0; // lower 24 bits
    bool is_repeat = false;

    static JVC24Frame fromBits(const esp32irpk::IRDecodedBits &in)
    {
      JVC24Frame out{};
      if (in.frame_type == esp32irpk::IRFrameType::REPEAT)
      {
        out.is_repeat = true;
        return out;
      }

      uint32_t mask = (1U << 24) - 1U;
      out.data = static_cast<uint32_t>(in.bits) & mask;
      return out;
    }

    esp32irpk::IRDecodedBits toBits() const
    {
      esp32irpk::IRDecodedBits out{};
      out.protocol_id = esp32irpk::IRProtocolID::JVC24;
      if (is_repeat)
      {
        out.frame_type = esp32irpk::IRFrameType::REPEAT;
        out.bit_length = 0;
        out.bits = 0xFFFFFFFFFFFFFFFFULL;
        return out;
      }

      uint32_t mask = (1U << 24) - 1U;
      out.frame_type = esp32irpk::IRFrameType::NORMAL;
      out.bit_length = 24;
      out.bits = static_cast<uint64_t>(data & mask);
      return out;
    }
  };

  struct JVC32Frame
  {
    uint32_t data = 0; // full 32 bits
    bool is_repeat = false;

    static JVC32Frame fromBits(const esp32irpk::IRDecodedBits &in)
    {
      JVC32Frame out{};
      if (in.frame_type == esp32irpk::IRFrameType::REPEAT)
      {
        out.is_repeat = true;
        return out;
      }

      out.data = static_cast<uint32_t>(in.bits & 0xFFFFFFFFULL);
      return out;
    }

    esp32irpk::IRDecodedBits toBits() const
    {
      esp32irpk::IRDecodedBits out{};
      out.protocol_id = esp32irpk::IRProtocolID::JVC32;
      if (is_repeat)
      {
        out.frame_type = esp32irpk::IRFrameType::REPEAT;
        out.bit_length = 0;
        out.bits = 0xFFFFFFFFFFFFFFFFULL;
        return out;
      }

      out.frame_type = esp32irpk::IRFrameType::NORMAL;
      out.bit_length = 32;
      out.bits = static_cast<uint64_t>(data);
      return out;
    }
  };

} // namespace esp32irpk::frames
