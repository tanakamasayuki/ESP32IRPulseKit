#pragma once

#include "../ESP32IRPulseKit.h"

namespace esp32irpk::specs
{
  // https://support.jvc.com/consumer/support/documents/RemoteCodes.pdf
  //
  // Standard JVC is a 16-bit protocol (8-bit address + 8-bit command),
  // LSB-first, ~38kHz carrier. IRP: {38k,525}<1,-1|1,-3>(16,-8,(D:8,F:8,1,-23)+)
  //
  // Note on repeats: a real JVC remote sends the 8400/4200 header only once and
  // then resends the 16-bit data block *without* the header for held-button
  // repeats. PulseKit models a standard header-led 16-bit frame (NORMAL); the
  // headerless-repeat form is not modeled as a distinct frame type. A header-led
  // frame each time still decodes correctly on standard receivers.

  // clang-format off
  inline constexpr IRProtocolSpec JVC = {
      .protocol_id      = IRProtocolID::JVC,
      .name             = "JVC",
      .scheme           = IRProtocolScheme::SPACE_ENC,
      .family           = IRProtocolFamily::NEC_LIKE,
      .header           = {.mark_us = 8400, .space_us = 4200},
      .one              = {.mark_us =  525, .space_us = 1575},
      .zero             = {.mark_us =  525, .space_us =  525},
      .trailer          = {.mark_us =  525, .space_us =    0},
      .gap_threshold_us = 20000,
      .idle_threshold_us= 42000,
      .carrier_hz       = 37900,
      .lsb_first        = true,
      .bit_length       = 16,
      .default_repeat_count = 0,
      .bit_tol_pct      = 30,
  };
  // clang-format on

} // namespace esp32irpk::specs

namespace esp32irpk::frames
{

  struct JVCFrame
  {
    uint8_t address = 0; // first byte on the wire (LSB-first)
    uint8_t command = 0; // second byte
    bool is_repeat = false;

    static JVCFrame fromBits(const esp32irpk::IRDecodedBits &in)
    {
      JVCFrame out{};
      if (in.frame_type == esp32irpk::IRFrameType::REPEAT)
      {
        out.is_repeat = true;
        return out;
      }
      out.address = static_cast<uint8_t>(in.bits & 0xFFu);
      out.command = static_cast<uint8_t>((in.bits >> 8) & 0xFFu);
      return out;
    }

    esp32irpk::IRDecodedBits toBits() const
    {
      esp32irpk::IRDecodedBits out{};
      out.protocol_id = esp32irpk::IRProtocolID::JVC;
      out.frame_type = esp32irpk::IRFrameType::NORMAL;
      out.bit_length = 16;
      out.bits = static_cast<uint64_t>(address) |
                 (static_cast<uint64_t>(command) << 8);
      return out;
    }
  };

} // namespace esp32irpk::frames
