#pragma once

#include "../ESP32IRPulseKit.h"
#include "AcCodec.h"

// Sharp air-conditioner support (the standard 13-byte SHARP_AC protocol). A setting
// is a single 13-byte pulse-distance frame, LSB-first, beginning with the fixed
// header bytes AA 5A CF 10, with a nibble-folded XOR checksum in the high nibble of
// byte 12. See SPEC §11.2.
//
// Frame mechanics (timing, header, nibble checksum) follow the documented format.
// The logical field map (byte/bit positions and the mode/fan codes) is verified
// field-for-field against IRremoteESP8266's IRSharpAc via the compat_matrix_ac
// studies (sharp_irremoteesp8266_*).
//
// This targets the default A907 model. The A705 / A903 models (which remap heat to
// fan mode and use different fan codes) carry a Model/Model2 bit and would be a
// `Model` parameter later (SPEC §11.2). Swing, ion, clean, timer and the special
// "feature" commands are documented but not settable here.
//
// Power lives in the 4-bit PowerSpecial field (byte 5): on = 3, off = 2. The Special
// byte (byte 10) records which button a real remote pressed; we always emit the
// "power" value (0x00), so toRaw is a complete state command, not a single-field
// toggle. Auto and Dry modes carry no temperature (Temp = 0), so temperature is a
// don't-care there.

namespace esp32irpk::ac::Sharp
{

  // Per-vendor enums; values are the wire codes. Mode lives in byte 6 bits 0-1.
  // (Auto and Fan share code 0b00; we expose Auto only.)
  enum class Mode : uint8_t
  {
    AUTO = 0,
    HEAT = 1,
    COOL = 2,
    DRY = 3,
  };

  // Fan lives in byte 6 bits 4-6. Wire codes are non-contiguous (auto=2, min=4,
  // med=3, high=5, max=7). Enum values ARE the wire codes.
  enum class Fan : uint8_t
  {
    AUTO = 2,
    MED_SPEED = 3,
    MIN_SPEED = 4,
    HIGH_SPEED = 5,
    MAX_SPEED = 7,
  };

  inline const char *toString(Mode m)
  {
    switch (m)
    {
    case Mode::AUTO: return "AUTO";
    case Mode::HEAT: return "HEAT";
    case Mode::COOL: return "COOL";
    case Mode::DRY: return "DRY";
    }
    return "?";
  }
  inline const char *toString(Fan f)
  {
    switch (f)
    {
    case Fan::AUTO: return "AUTO";
    case Fan::MIN_SPEED: return "MIN_SPEED";
    case Fan::MED_SPEED: return "MED_SPEED";
    case Fan::HIGH_SPEED: return "HIGH_SPEED";
    case Fan::MAX_SPEED: return "MAX_SPEED";
    }
    return "?";
  }

  namespace detail
  {
    // Standard SHARP_AC timing. LSB-first. The zero-space is emitted at 460us
    // rather than the nominal 500 so that, after the receiver's mark/space bias,
    // the recovered space stays under IRremoteESP8266's zero-space ceiling
    // (~563us); the wide tol_pct still recovers its 500us zero on the way in.
    inline constexpr AcTiming kTiming = {
        /*header_mark_us=*/3800,
        /*header_space_us=*/1900,
        /*bit_mark_us=*/470,
        /*zero_space_us=*/460,
        /*one_space_us=*/1400,
        /*trailer_mark_us=*/470,
        /*frame_gap_us=*/29000,
        /*tol_pct=*/30,
        /*lsb_first=*/true,
    };

    inline constexpr size_t kBytes = 13;

    // Fixed header bytes 0-3 (AA 5A CF 10).
    inline constexpr uint8_t kHdr0 = 0xAA;
    inline constexpr uint8_t kHdr1 = 0x5A;
    inline constexpr uint8_t kHdr2 = 0xCF;
    inline constexpr uint8_t kHdr3 = 0x10;

    inline constexpr size_t kOffTemp = 4;     // bits 0-3 Temp, bit 4 Model, hi bits 0xC0 (Cool/Heat)
    inline constexpr size_t kOffPower = 5;    // bits 4-7 PowerSpecial
    inline constexpr size_t kOffModeFan = 6;  // bits 0-1 Mode, bit 3 Clean, bits 4-6 Fan
    inline constexpr size_t kOffSpecial = 10; // "which button" byte
    inline constexpr size_t kOffSum = 12;     // bits 4-7 checksum

    inline constexpr uint8_t kPowerOff = 2;
    inline constexpr uint8_t kPowerOn = 3;
    inline constexpr uint8_t kSpecialPower = 0x00;

    inline constexpr uint8_t kMinTempC = 15;
    inline constexpr uint8_t kMaxTempC = 30;
    inline constexpr uint8_t kFanAuto = 2;

    inline bool isAutoOrDry(uint8_t mode) { return mode == 0 /*AUTO*/ || mode == 3 /*DRY*/; }

    // Nibble-folded XOR checksum: XOR all bytes except byte 12's high nibble, then
    // fold to a nibble. Stored in byte 12's high nibble.
    inline uint8_t calcChecksum(const uint8_t *b)
    {
      uint8_t x = 0;
      for (size_t i = 0; i < kBytes - 1; ++i)
        x ^= b[i];
      x ^= static_cast<uint8_t>(b[kBytes - 1] & 0x0Fu); // low nibble of byte 12
      x ^= static_cast<uint8_t>((x >> 4) & 0x0Fu);      // fold high nibble in
      return static_cast<uint8_t>(x & 0x0Fu);
    }
    inline bool checksumOk(const uint8_t *b)
    {
      return ((b[kOffSum] >> 4) & 0x0Fu) == calcChecksum(b);
    }

    inline Mode codeToMode(uint8_t c)
    {
      switch (c & 0x3u)
      {
      case 1: return Mode::HEAT;
      case 2: return Mode::COOL;
      case 3: return Mode::DRY;
      default: return Mode::AUTO;
      }
    }
    inline Fan codeToFan(uint8_t c)
    {
      switch (c & 0x7u)
      {
      case 3: return Fan::MED_SPEED;
      case 4: return Fan::MIN_SPEED;
      case 5: return Fan::HIGH_SPEED;
      case 7: return Fan::MAX_SPEED;
      default: return Fan::AUTO; // 2 and any unknown
      }
    }
  } // namespace detail

  struct Frame
  {
    static constexpr size_t kBytes = detail::kBytes; // 13
    static constexpr size_t kMaxTicks = 64 + 13 * 16; // header + 104 bits + trailer

    // raw state: a known-good A907 frame (on / cool / 24C / fan auto), checksum
    // precomputed. Header AA 5A CF 10; byte 4 = 0xC0 | (degC-15) in Cool/Heat
    // (IRSharpAc fixes the temp byte's high bits to 0xC0); Special = power (0x00).
    uint8_t bytes[kBytes] = {0xAA, 0x5A, 0xCF, 0x10, 0xC9, 0x31, 0x22,
                             0x00, 0x08, 0x80, 0x00, 0xE0, 0x51};
    uint16_t byte_length = 0;
    bool checksum_ok = false;

    bool power() const
    {
      const uint8_t ps = (bytes[detail::kOffPower] >> 4) & 0x0Fu;
      return ps != 0 && ps != detail::kPowerOff; // 1 or 3 = on
    }
    void setPower(bool on)
    {
      const uint8_t ps = on ? detail::kPowerOn : detail::kPowerOff;
      bytes[detail::kOffPower] =
          static_cast<uint8_t>((bytes[detail::kOffPower] & 0x0Fu) | (ps << 4));
      bytes[detail::kOffSpecial] = detail::kSpecialPower;
    }

    Mode mode() const { return detail::codeToMode(bytes[detail::kOffModeFan]); }
    void setMode(Mode m)
    {
      bytes[detail::kOffModeFan] = static_cast<uint8_t>(
          (bytes[detail::kOffModeFan] & ~0x03u) | (static_cast<uint8_t>(m) & 0x03u));
      bytes[detail::kOffSpecial] = detail::kSpecialPower;
    }

    // Whole-degree temperature, byte 4 low nibble = °C - 15, clamped 15..30. Auto/Dry
    // carry no temperature (forced to 0 by toRaw).
    uint8_t temperatureC() const
    {
      return static_cast<uint8_t>((bytes[detail::kOffTemp] & 0x0Fu) + detail::kMinTempC);
    }
    void setTemperatureC(uint8_t c)
    {
      if (c < detail::kMinTempC) c = detail::kMinTempC;
      if (c > detail::kMaxTempC) c = detail::kMaxTempC;
      bytes[detail::kOffTemp] = static_cast<uint8_t>(
          (bytes[detail::kOffTemp] & 0xF0u) | ((c - detail::kMinTempC) & 0x0Fu));
    }

    Fan fan() const { return detail::codeToFan((bytes[detail::kOffModeFan] >> 4) & 0x07u); }
    void setFan(Fan f)
    {
      bytes[detail::kOffModeFan] = static_cast<uint8_t>(
          (bytes[detail::kOffModeFan] & ~(0x07u << 4)) | ((static_cast<uint8_t>(f) & 0x07u) << 4));
    }

    void printTo(Print &out) const
    {
      printAcSummary(out, "Sharp", power(), toString(mode()),
                     static_cast<float>(temperatureC()), toString(fan()), bytes,
                     byte_length, checksum_ok);
    }

    void printSetterSnippet(Print &out) const
    {
      out.println("// send code (Sharp AC, editable -- lossy: swing/ion/timer use defaults):");
      out.println("esp32irpk::ac::Sharp::Frame f;");
      out.print("f.setPower(");
      out.print(power() ? "true" : "false");
      out.println(");");
      out.print("f.setMode(esp32irpk::ac::Sharp::Mode::");
      out.print(toString(mode()));
      out.println(");");
      out.print("f.setTemperatureC(");
      out.print((unsigned)temperatureC());
      out.println(");");
      out.print("f.setFan(esp32irpk::ac::Sharp::Fan::");
      out.print(toString(fan()));
      out.println(");");
      out.println("esp32irpk::ac::send(tx, f);");
    }

    // RAW ticks -> state bytes. false if not a standard Sharp frame (fixed header
    // AA 5A CF 10); checksum validity is reported separately via `checksum_ok`.
    static bool fromRaw(const esp32irpk::IRRawTickView &raw, Frame &out)
    {
      out = Frame{};
      if (!raw.ticks)
        return false;
      size_t pos = 0;
      uint8_t b[detail::kBytes] = {};
      if (rawFrameToBytes(raw, pos, detail::kTiming, b, detail::kBytes) != detail::kBytes * 8)
        return false;
      if (b[0] != detail::kHdr0 || b[1] != detail::kHdr1 ||
          b[2] != detail::kHdr2 || b[3] != detail::kHdr3)
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

    // state -> RAW ticks. Forces the fixed header and the field invariants IRSharpAc
    // enforces (Auto/Dry => Fan Auto + Temp 0; Special => power), recomputes the
    // nibble checksum, then renders one LSB-first 13-byte frame.
    bool toRaw(esp32irpk::IRRawTickBuffer &out) const
    {
      uint8_t buf[kBytes];
      for (size_t i = 0; i < kBytes; ++i)
        buf[i] = bytes[i];
      buf[0] = detail::kHdr0; buf[1] = detail::kHdr1;
      buf[2] = detail::kHdr2; buf[3] = detail::kHdr3;
      buf[detail::kOffSpecial] = detail::kSpecialPower;

      const uint8_t modeCode = buf[detail::kOffModeFan] & 0x03u;
      if (detail::isAutoOrDry(modeCode))
      {
        // Auto/Dry carry no temperature: IRSharpAc's setTemp zeroes the whole
        // temp byte. Fan is NOT forced: setFan overrides regardless of mode, so
        // it round-trips.
        buf[detail::kOffTemp] = 0x00;
      }
      else
      {
        // Cool/Heat (A907): IRSharpAc's setTemp writes 0xC0 (Model bit clear) to
        // the temp byte's high bits, then the degC-15 nibble. Match it so our
        // frame is byte-identical to a real A907 remote.
        buf[detail::kOffTemp] =
            static_cast<uint8_t>(0xC0u | (buf[detail::kOffTemp] & 0x0Fu));
      }
      buf[detail::kOffSum] = static_cast<uint8_t>(
          (buf[detail::kOffSum] & 0x0Fu) | (detail::calcChecksum(buf) << 4));

      out.len = 0;
      return bytesFrameToRaw(buf, kBytes * 8, detail::kTiming, out);
    }
  };

} // namespace esp32irpk::ac::Sharp
