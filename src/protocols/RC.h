#pragma once

#include "../ESP32IRPulseKit.h"

namespace esp32irpk::specs
{

  // RC-5: bi-phase (Manchester) 14bit
  // clang-format off
  inline constexpr IRProtocolSpec RC5 = {
      .protocol_id = IRProtocolID::RC5,
      .name = "RC5",
      .scheme = IRProtocolScheme::BIPHASE,
      .family = IRProtocolFamily::RC5,
      .lsb_first = false,
      .bit_length = 14,
      .bit_tol_pct = 25,
  };
  // clang-format on

  // RC-6 Mode 0 (16bit payload, total 21 bits including start/mode/toggle)
  // clang-format off
  inline constexpr IRProtocolSpec RC6_M0_16 = {
      .protocol_id    = IRProtocolID::RC6_M0_16,
      .name           = "RC6_M0_16",
      .scheme         = IRProtocolScheme::BIPHASE,
      .family         = IRProtocolFamily::RC6,
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
  };

} // namespace esp32irpk::frames
