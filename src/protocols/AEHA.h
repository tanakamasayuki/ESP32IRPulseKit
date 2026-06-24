#pragma once

#include "../ESP32IRPulseKit.h"

namespace esp32irpk::specs
{

  // clang-format off
  inline constexpr IRProtocolSpec AEHA = {
      .protocol_id      = IRProtocolID::AEHA,
      .name             = "AEHA",
      .scheme           = IRProtocolScheme::SPACE_ENC,
      .family           = IRProtocolFamily::AEHA,
      .header           = {.mark_us = 3400, .space_us = 1700},
      .one              = {.mark_us =  425, .space_us = 1275},
      .zero             = {.mark_us =  425, .space_us =  425},
      .trailer          = {.mark_us =  425, .space_us =    0},
      .gap_threshold_us = 10800,
      .idle_threshold_us= 15000,
      .carrier_hz       = kDefaultCarrierHz,
      .lsb_first        = true,
      .bit_length       = 64,   // nominal upper bound
      .min_bit_length   = 48,
      .max_bit_length   = 64,
      .default_repeat_count = 0,
      .bit_tol_pct      = 30,
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

} // namespace esp32irpk::frames

namespace esp32irpk::bits
{

  inline esp32irpk::IRDecodedBits aeha(uint64_t data, uint16_t bit_length)
  {
    esp32irpk::frames::AEHAFrame frame{};
    frame.data = data;
    frame.bit_length = bit_length;
    return frame.toBits();
  }

} // namespace esp32irpk::bits
