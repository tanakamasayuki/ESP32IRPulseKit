#pragma once

#include "../ESP32IRPulseKit.h"

namespace esp32irpk::specs
{
  // Samsung Electronics, S3F80KB IR REMOTE CONTROLLER (Application Note, Oct 2008)

  // clang-format off
  inline constexpr IRProtocolSpec SAMSUNG32 = {
      .protocol_id      = IRProtocolID::SAMSUNG32,
      .name             = "SAMSUNG32",
      .scheme           = IRProtocolScheme::SPACE_ENC,
      .family           = IRProtocolFamily::NEC_LIKE,
      .header           = {.mark_us = 4500, .space_us = 4500},
      .one              = {.mark_us =  560, .space_us = 1690},
      .zero             = {.mark_us =  560, .space_us =  560},
      .trailer          = {.mark_us =  560, .space_us =    0},
      .gap_threshold_us = 30000,
      .idle_threshold_us= 30000,
      .lsb_first        = true,
      .bit_length       = 32,
      .default_repeat_count = 0,
  };
  // clang-format on

  // clang-format off
  inline constexpr IRProtocolSpec SAMSUNG36 = {
      .protocol_id      = IRProtocolID::SAMSUNG36,
      .name             = "SAMSUNG36",
      .scheme           = IRProtocolScheme::SPACE_ENC,
      .family           = IRProtocolFamily::NEC_LIKE,
      .header           = {.mark_us = 4500, .space_us = 4500},
      .one              = {.mark_us =  560, .space_us = 1690},
      .zero             = {.mark_us =  560, .space_us =  560},
      .trailer          = {.mark_us =  560, .space_us =    0},
      .gap_threshold_us = 30000,
      .idle_threshold_us= 30000,
      .lsb_first        = true,
      .bit_length       = 36,
      .default_repeat_count = 0,
  };
  // clang-format on

} // namespace esp32irpk::specs

namespace esp32irpk::frames
{

  struct Samsung32Frame
  {
    uint16_t address = 0;
    uint16_t command = 0;
    bool is_repeat = false;

    static Samsung32Frame fromBits(const esp32irpk::IRDecodedBits &in)
    {
      Samsung32Frame out{};
      if (in.frame_type == esp32irpk::IRFrameType::REPEAT)
      {
        out.is_repeat = true;
        return out;
      }

      uint64_t bits = in.bits;
      out.address = static_cast<uint16_t>(bits & 0xFFFFULL);
      out.command = static_cast<uint16_t>((bits >> 16) & 0xFFFFULL);
      return out;
    }

    esp32irpk::IRDecodedBits toBits() const
    {
      esp32irpk::IRDecodedBits out{};
      out.protocol_id = esp32irpk::IRProtocolID::SAMSUNG32;
      if (is_repeat)
      {
        out.frame_type = esp32irpk::IRFrameType::REPEAT;
        out.bit_length = 0;
        out.bits = 0xFFFFFFFFFFFFFFFFULL;
        return out;
      }

      uint64_t bits = 0;
      bits |= static_cast<uint64_t>(address);
      bits |= (static_cast<uint64_t>(command) << 16);

      out.frame_type = esp32irpk::IRFrameType::NORMAL;
      out.bit_length = 32;
      out.bits = bits;
      return out;
    }
  };

  struct Samsung36Frame
  {
    uint16_t address = 0;
    uint32_t command = 0;
    bool is_repeat = false;

    static Samsung36Frame fromBits(const esp32irpk::IRDecodedBits &in)
    {
      Samsung36Frame out{};
      if (in.frame_type == esp32irpk::IRFrameType::REPEAT)
      {
        out.is_repeat = true;
        return out;
      }

      uint64_t bits = in.bits;
      out.address = static_cast<uint16_t>(bits & 0xFFFFULL);
      out.command = static_cast<uint32_t>((bits >> 16) & 0xFFFFFULL);
      return out;
    }

    esp32irpk::IRDecodedBits toBits() const
    {
      esp32irpk::IRDecodedBits out{};
      out.protocol_id = esp32irpk::IRProtocolID::SAMSUNG36;
      if (is_repeat)
      {
        out.frame_type = esp32irpk::IRFrameType::REPEAT;
        out.bit_length = 0;
        out.bits = 0xFFFFFFFFFFFFFFFFULL;
        return out;
      }

      uint64_t bits = 0;
      bits |= static_cast<uint64_t>(address);
      bits |= (static_cast<uint64_t>(command & 0xFFFFFULL) << 16);

      out.frame_type = esp32irpk::IRFrameType::NORMAL;
      out.bit_length = 36;
      out.bits = bits;
      return out;
    }
  };

} // namespace esp32irpk::frames
