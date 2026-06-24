#pragma once

#include "../ESP32IRPulseKit.h"

namespace esp32irpk::specs
{

  // RC-5: bi-phase (Manchester) 14bit
  // clang-format off
  inline constexpr IRProtocolSpec RC5 = {
      .protocol_id    = IRProtocolID::RC5,
      .name           = "RC5",
      .scheme         = IRProtocolScheme::BIPHASE,
      .family         = IRProtocolFamily::RC5,
      .carrier_hz     = 36000,
      .lsb_first      = false,
      .bit_length     = 14,
      .bit_tol_pct    = 25,
  };
  // clang-format on

  // RC-6 Mode 0 (16bit payload, total 21 bits including start/mode/toggle)
  // clang-format off
  inline constexpr IRProtocolSpec RC6_M0_16 = {
      .protocol_id    = IRProtocolID::RC6_M0_16,
      .name           = "RC6_M0_16",
      .scheme         = IRProtocolScheme::BIPHASE,
      .family         = IRProtocolFamily::RC6,
      .carrier_hz     = 36000,
      .lsb_first      = false,
      .bit_length     = 21, // start(1)+mode(3)+toggle(1)+payload(16)
      .bit_tol_pct    = 25,
  };
  // clang-format on

  // RC-6 Mode 6 (32bit payload, total 36 bits including start/mode)
  // clang-format off
  inline constexpr IRProtocolSpec RC6_M6_32 = {
      .protocol_id    = IRProtocolID::RC6_M6_32,
      .name           = "RC6_M6_32",
      .scheme         = IRProtocolScheme::BIPHASE,
      .family         = IRProtocolFamily::RC6,
      .carrier_hz     = 36000,
      .lsb_first      = false,
      .bit_length     = 36, // start(1)+mode(3)+payload(32)
      .bit_tol_pct    = 25,
  };
  // clang-format on

} // namespace esp32irpk::specs

namespace esp32irpk::frames
{
  struct RC5Frame
  {
    uint16_t data = 0; // 14bit payload as-is
    static RC5Frame fromBits(const esp32irpk::IRDecodedBits &in)
    {
      RC5Frame out{};
      out.data = static_cast<uint16_t>(in.bits & 0x3FFFu);
      return out;
    }

    esp32irpk::IRDecodedBits toBits() const
    {
      esp32irpk::IRDecodedBits out{};
      out.protocol_id = esp32irpk::IRProtocolID::RC5;
      out.frame_type = esp32irpk::IRFrameType::NORMAL;
      out.bit_length = 14;
      out.bits = static_cast<uint64_t>(data & 0x3FFFu);
      return out;
    }
  };

  struct RC6M0Frame
  {
    uint32_t data = 0; // lower 21 bits (toggle+payload)
    static RC6M0Frame fromBits(const esp32irpk::IRDecodedBits &in)
    {
      RC6M0Frame out{};
      out.data = static_cast<uint32_t>(in.bits & 0x1FFFFFu);
      return out;
    }

    esp32irpk::IRDecodedBits toBits() const
    {
      esp32irpk::IRDecodedBits out{};
      out.protocol_id = esp32irpk::IRProtocolID::RC6_M0_16;
      out.frame_type = esp32irpk::IRFrameType::NORMAL;
      out.bit_length = 21;
      out.bits = static_cast<uint64_t>(data & 0x1FFFFFu);
      return out;
    }
  };

  struct RC6M6Frame
  {
    uint64_t data = 0; // lower 36 bits
    static RC6M6Frame fromBits(const esp32irpk::IRDecodedBits &in)
    {
      RC6M6Frame out{};
      out.data = in.bits & 0xFFFFFFFFFULL;
      return out;
    }

    esp32irpk::IRDecodedBits toBits() const
    {
      esp32irpk::IRDecodedBits out{};
      out.protocol_id = esp32irpk::IRProtocolID::RC6_M6_32;
      out.frame_type = esp32irpk::IRFrameType::NORMAL;
      out.bit_length = 36;
      out.bits = data & 0xFFFFFFFFFULL;
      return out;
    }
  };

} // namespace esp32irpk::frames

namespace esp32irpk::bits
{

  inline esp32irpk::IRDecodedBits rc5(uint16_t data)
  {
    esp32irpk::frames::RC5Frame frame{};
    frame.data = data;
    return frame.toBits();
  }

  inline esp32irpk::IRDecodedBits rc6m0(uint32_t data)
  {
    esp32irpk::frames::RC6M0Frame frame{};
    frame.data = data;
    return frame.toBits();
  }

  inline esp32irpk::IRDecodedBits rc6m6(uint64_t data)
  {
    esp32irpk::frames::RC6M6Frame frame{};
    frame.data = data;
    return frame.toBits();
  }

} // namespace esp32irpk::bits
