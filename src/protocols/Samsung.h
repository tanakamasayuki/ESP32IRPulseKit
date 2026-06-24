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
      .carrier_hz       = kDefaultCarrierHz,
      .lsb_first        = true,
      .bit_length       = 32,
      .default_repeat_count = 0,
  };
  // clang-format on

  // SAMSUNG36 is a two-block waveform, sent MSB-first:
  //   header(4515/4438) | block1: top 16 bits | block1 footer mark(512) +
  //   separator space(4438) | block2: low 20 bits | trailer mark(512) [+ gap]
  // The 36-bit value is laid out MSB-first: bits[35..20] = block1 (address,
  // 16 bits), bits[19..0] = block2 (command, 20 bits). The block split and the
  // mid-frame separator are not expressible by the generic SPACE_ENC
  // encoder/decoder, so SAMSUNG36 uses a protocol-specific encode/decode path
  // (dispatched by protocol_id), the same way RC5/RC6 do within BIPHASE.
  // `header.space_us` doubles as the inter-block separator space; `trailer.mark_us`
  // doubles as each block's footer mark.
  // clang-format off
  inline constexpr IRProtocolSpec SAMSUNG36 = {
      .protocol_id      = IRProtocolID::SAMSUNG36,
      .name             = "SAMSUNG36",
      .scheme           = IRProtocolScheme::SPACE_ENC,
      .family           = IRProtocolFamily::NEC_LIKE,
      .header           = {.mark_us = 4515, .space_us = 4438},
      .one              = {.mark_us =  512, .space_us = 1468},
      .zero             = {.mark_us =  512, .space_us =  490},
      .trailer          = {.mark_us =  512, .space_us =    0},
      .gap_threshold_us = 30000,
      .idle_threshold_us= 30000,
      .carrier_hz       = kDefaultCarrierHz,
      .lsb_first        = false, // MSB-first (two-block)
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

      // MSB-first 36-bit value: bits[35..20] = address (block 1, 16 bits),
      // bits[19..0] = command (block 2, 20 bits).
      uint64_t bits = in.bits;
      out.address = static_cast<uint16_t>((bits >> 20) & 0xFFFFULL);
      out.command = static_cast<uint32_t>(bits & 0xFFFFFULL);
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

      // MSB-first 36-bit value: address occupies the top 16 bits (block 1),
      // command the low 20 bits (block 2).
      uint64_t bits = (static_cast<uint64_t>(address) << 20) |
                      static_cast<uint64_t>(command & 0xFFFFFULL);

      out.frame_type = esp32irpk::IRFrameType::NORMAL;
      out.bit_length = 36;
      out.bits = bits;
      return out;
    }
  };

} // namespace esp32irpk::frames

namespace esp32irpk::bits
{

  inline esp32irpk::IRDecodedBits samsung32(uint16_t address, uint16_t command)
  {
    esp32irpk::frames::Samsung32Frame frame{};
    frame.address = address;
    frame.command = command;
    return frame.toBits();
  }

  inline esp32irpk::IRDecodedBits samsung36(uint16_t address, uint32_t command)
  {
    esp32irpk::frames::Samsung36Frame frame{};
    frame.address = address;
    frame.command = command;
    return frame.toBits();
  }

} // namespace esp32irpk::bits
