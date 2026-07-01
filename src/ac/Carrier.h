#pragma once

#include "../ESP32IRPulseKit.h"
#include "AcCodec.h"

// Carrier air-conditioner support (the CARRIER_AC64 protocol: Carrier/Surrey
// 619EGX / 53NGK inverter remotes). A full setting is a single 8-byte (64-bit)
// pulse-distance frame, LSB-first, beginning with the fixed signature 0x84 0x55,
// with a 4-bit nibble-sum checksum in the low nibble of byte 2. See SPEC §11.2.
//
// Frame mechanics (timing, signature, checksum) follow the documented format. The
// logical field map (byte/bit positions and the mode/fan codes) is verified
// field-for-field against IRremoteESP8266's IRCarrierAc64 via the compat_matrix_ac
// studies (carrier_irremoteesp8266_*).
//
// This targets the 64-bit CARRIER_AC64 format (no `Model` parameter). The other
// Carrier wire formats (AC / AC40 / AC84 / AC128) are different frames and would be
// separate Frame types (SPEC §11.2). Sleep and the on/off timers are documented but
// not settable here; SwingV is.

namespace esp32irpk::ac::Carrier
{

  // Per-vendor enums; values are the wire codes. Mode lives in byte 2 bits 4-5.
  // There is no Auto or Dry mode in CARRIER_AC64.
  enum class Mode : uint8_t
  {
    HEAT = 1,
    COOL = 2,
    FAN = 3,
  };

  // Fan lives in byte 2 bits 6-7. Enum values ARE the wire codes.
  enum class Fan : uint8_t
  {
    AUTO = 0,
    LOW_SPEED = 1,
    MED_SPEED = 2,
    HIGH_SPEED = 3,
  };

  inline const char *toString(Mode m)
  {
    switch (m)
    {
    case Mode::HEAT: return "HEAT";
    case Mode::COOL: return "COOL";
    case Mode::FAN: return "FAN";
    }
    return "?";
  }
  inline const char *toString(Fan f)
  {
    switch (f)
    {
    case Fan::AUTO: return "AUTO";
    case Fan::LOW_SPEED: return "LOW_SPEED";
    case Fan::MED_SPEED: return "MED_SPEED";
    case Fan::HIGH_SPEED: return "HIGH_SPEED";
    }
    return "?";
  }

  namespace detail
  {
    // Standard CARRIER_AC64 timing (kCarrierAcFreq = 38kHz). LSB-first.
    inline constexpr AcTiming kTiming = {
        /*header_mark_us=*/8940,
        /*header_space_us=*/4556,
        /*bit_mark_us=*/503,
        /*zero_space_us=*/615,
        /*one_space_us=*/1736,
        /*trailer_mark_us=*/503,
        /*frame_gap_us=*/20000, // CARRIER_AC64 is sent once; its real trailing gap is
                                // kDefaultMessageGap (100ms) but that silence is never
                                // captured, so any value > one_space (for the frame-end
                                // threshold) that fits uint16_t is fine.
        /*tol_pct=*/40,         // wide decode window (see [[ac-decode-tolerance-loose]])
        /*lsb_first=*/true,
    };

    inline constexpr size_t kBytes = 8;

    inline constexpr uint8_t kSig0 = 0x84;
    inline constexpr uint8_t kSig1 = 0x55;

    inline constexpr size_t kOffSumModeFan = 2; // Sum bits0-3, Mode bits4-5, Fan bits6-7
    inline constexpr size_t kOffTempSwing = 3;  // Temp bits0-3, SwingV bit5
    inline constexpr size_t kOffPower = 4;       // Power bit4, timer-enables bits5-6, Sleep bit7
    inline constexpr size_t kUnusedByte = 5;     // always 0

    inline constexpr uint8_t kPowerMask = 0x10;  // byte 4 bit 4
    inline constexpr uint8_t kSwingVMask = 0x20; // byte 3 bit 5
    inline constexpr uint8_t kMinTempC = 16;
    inline constexpr uint8_t kMaxTempC = 30;

    // 4-bit checksum (IRCarrierAc64::calcChecksum): sum every nibble above the
    // checksum itself -- byte 2's high nibble (Mode/Fan) plus both nibbles of
    // bytes 3-7 -- mod 16. The two signature bytes 0-1 are NOT included.
    inline uint8_t calcChecksum(const uint8_t *b)
    {
      uint8_t sum = static_cast<uint8_t>((b[kOffSumModeFan] >> 4) & 0x0Fu);
      for (size_t i = 3; i < kBytes; ++i)
        sum = static_cast<uint8_t>(sum + (b[i] & 0x0Fu) + ((b[i] >> 4) & 0x0Fu));
      return static_cast<uint8_t>(sum & 0x0Fu);
    }
    inline void writeChecksum(uint8_t *b)
    {
      b[kOffSumModeFan] = static_cast<uint8_t>((b[kOffSumModeFan] & 0xF0u) | calcChecksum(b));
    }
    inline bool checksumOk(const uint8_t *b)
    {
      return static_cast<uint8_t>(b[kOffSumModeFan] & 0x0Fu) == calcChecksum(b);
    }

    inline Mode codeToMode(uint8_t c)
    {
      switch (c & 0x3u)
      {
      case 1: return Mode::HEAT;
      case 3: return Mode::FAN;
      default: return Mode::COOL; // 2, and the invalid 0 (IRCarrierAc64 defaults to Cool)
      }
    }
    inline Fan codeToFan(uint8_t c)
    {
      switch (c & 0x3u)
      {
      case 1: return Fan::LOW_SPEED;
      case 2: return Fan::MED_SPEED;
      case 3: return Fan::HIGH_SPEED;
      default: return Fan::AUTO;
      }
    }
  } // namespace detail

  struct Frame
  {
    static constexpr size_t kBytes = detail::kBytes; // 8
    static constexpr size_t kMaxTicks = 2 + 64 * 2 + 2 + 4; // header + 64 bits + trailer + gap

    // raw state: a known-good frame (power on / cool / 24C / fan auto / no swing,
    // timers off), checksum precomputed. 84 55 2B 08 10 00 00 00.
    uint8_t bytes[kBytes] = {0x84, 0x55, 0x2B, 0x08, 0x10, 0x00, 0x00, 0x00};
    uint16_t byte_length = 0;
    bool checksum_ok = false;

    bool power() const { return (bytes[detail::kOffPower] & detail::kPowerMask) != 0; }
    void setPower(bool on)
    {
      if (on)
        bytes[detail::kOffPower] |= detail::kPowerMask;
      else
        bytes[detail::kOffPower] &= static_cast<uint8_t>(~detail::kPowerMask);
    }

    Mode mode() const { return detail::codeToMode((bytes[detail::kOffSumModeFan] >> 4) & 0x03u); }
    void setMode(Mode m)
    {
      bytes[detail::kOffSumModeFan] = static_cast<uint8_t>(
          (bytes[detail::kOffSumModeFan] & ~(0x03u << 4)) | ((static_cast<uint8_t>(m) & 0x03u) << 4));
    }

    Fan fan() const { return detail::codeToFan((bytes[detail::kOffSumModeFan] >> 6) & 0x03u); }
    void setFan(Fan f)
    {
      bytes[detail::kOffSumModeFan] = static_cast<uint8_t>(
          (bytes[detail::kOffSumModeFan] & ~(0x03u << 6)) | ((static_cast<uint8_t>(f) & 0x03u) << 6));
    }

    // Whole-degree temperature, byte 3 low nibble = °C - 16, clamped 16..30.
    uint8_t temperatureC() const
    {
      return static_cast<uint8_t>((bytes[detail::kOffTempSwing] & 0x0Fu) + detail::kMinTempC);
    }
    void setTemperatureC(uint8_t c)
    {
      if (c < detail::kMinTempC) c = detail::kMinTempC;
      if (c > detail::kMaxTempC) c = detail::kMaxTempC;
      bytes[detail::kOffTempSwing] = static_cast<uint8_t>(
          (bytes[detail::kOffTempSwing] & 0xF0u) | ((c - detail::kMinTempC) & 0x0Fu));
    }

    bool swingV() const { return (bytes[detail::kOffTempSwing] & detail::kSwingVMask) != 0; }
    void setSwingV(bool on)
    {
      if (on)
        bytes[detail::kOffTempSwing] |= detail::kSwingVMask;
      else
        bytes[detail::kOffTempSwing] &= static_cast<uint8_t>(~detail::kSwingVMask);
    }

    void printTo(Print &out) const
    {
      printAcSummary(out, "Carrier", power(), toString(mode()),
                     static_cast<float>(temperatureC()), toString(fan()), bytes,
                     byte_length, checksum_ok);
      out.print("// swingV=");
      out.println(swingV() ? "on" : "off");
    }

    void printSetterSnippet(Print &out) const
    {
      out.println("// send code (Carrier AC, editable -- lossy: sleep/timers use defaults):");
      out.println("esp32irpk::ac::Carrier::Frame f;");
      out.print("f.setPower(");
      out.print(power() ? "true" : "false");
      out.println(");");
      out.print("f.setMode(esp32irpk::ac::Carrier::Mode::");
      out.print(toString(mode()));
      out.println(");");
      out.print("f.setTemperatureC(");
      out.print((unsigned)temperatureC());
      out.println(");");
      out.print("f.setFan(esp32irpk::ac::Carrier::Fan::");
      out.print(toString(fan()));
      out.println(");");
      out.print("f.setSwingV(");
      out.print(swingV() ? "true" : "false");
      out.println(");");
      out.println("esp32irpk::ac::send(tx, f);");
    }

    // RAW ticks -> state bytes. false if not a standard CARRIER_AC64 frame; checksum
    // validity is reported separately via `checksum_ok`. LSB-first.
    static bool fromRaw(const esp32irpk::IRRawTickView &raw, Frame &out)
    {
      out = Frame{};
      if (!raw.ticks)
        return false;
      size_t pos = 0;
      uint8_t b[detail::kBytes] = {};
      if (rawFrameToBytes(raw, pos, detail::kTiming, b, detail::kBytes) != detail::kBytes * 8)
        return false;
      // Fixed signature gate (also disambiguates from the ~9010/4505 Kelvinator/Gree
      // header, which is within tolerance of Carrier's 8940/4556).
      if (b[0] != detail::kSig0 || b[1] != detail::kSig1)
        return false;
      for (size_t i = 0; i < detail::kBytes; ++i)
        out.bytes[i] = b[i];
      out.byte_length = kBytes;
      out.checksum_ok = detail::checksumOk(out.bytes);
      return true;
    }

    // Decoded state bytes -> Frame, without RAW ticks. `len` must equal `kBytes`.
    static bool fromBytes(const uint8_t *state, size_t len, Frame &out)
    {
      out = Frame{};
      if (!state || len != kBytes)
        return false;
      for (size_t i = 0; i < kBytes; ++i)
        out.bytes[i] = state[i];
      out.byte_length = kBytes;
      out.checksum_ok = detail::checksumOk(out.bytes);
      return true;
    }

    // state -> RAW ticks. Rewrites the fixed signature bytes, clears the unused
    // byte, recomputes the nibble checksum, then renders the LSB-first 8-byte frame
    // once (CARRIER_AC64 is sent once, kCarrierAc64MinRepeat == kNoRepeat).
    bool toRaw(esp32irpk::IRRawTickBuffer &out) const
    {
      uint8_t buf[kBytes];
      for (size_t i = 0; i < kBytes; ++i)
        buf[i] = bytes[i];
      buf[0] = detail::kSig0;
      buf[1] = detail::kSig1;
      buf[detail::kUnusedByte] = 0x00;
      detail::writeChecksum(buf);

      out.len = 0;
      return bytesFrameToRaw(buf, kBytes * 8, detail::kTiming, out);
    }
  };

} // namespace esp32irpk::ac::Carrier
