#pragma once

#include "../ESP32IRPulseKit.h"
#include "AcCodec.h"

// Haier air-conditioner support (the 9-byte HAIER_AC protocol, older HSU-/YR-
// series remotes). A setting is a single 9-byte frame, MSB-first, that begins with
// an unusual DOUBLE header — a 3000/3000 pre-header followed by the 3000/4300 main
// header — starts with the fixed prefix 0xA5, and ends with a plain sum checksum.
// See SPEC §11.2.
//
// This protocol is command-oriented: byte 1's low nibble is a `Command` code
// recording which button was pressed (On/Off/Mode/Fan/TempUp/…). There is no
// persistent power bit — power is expressed as the On/Off command, so `setPower`
// writes that command and `power()` reads "not the Off command" (compare Toshiba,
// where power lives in the mode field). The full state (mode/fan/temp/swingV) is
// always present in the frame regardless of the command.
//
// Frame mechanics (double header, prefix, sum checksum) follow the documented
// format; the logical field map is verified field-for-field against
// IRremoteESP8266's IRHaierAC via the compat_matrix_ac studies. This targets the
// 9-byte HAIER_AC format (no `Model` parameter); the YRW02 (14-byte), AC160
// (20-byte) and AC176 (22-byte) remotes are different frames (SPEC §11.2). Timers,
// sleep and health are documented but not settable.

namespace esp32irpk::ac::Haier
{

  // Per-vendor enums; values are the wire codes. Mode lives in byte 6 bits 5-7.
  enum class Mode : uint8_t
  {
    AUTO = 0,
    COOL = 1,
    DRY = 2,
    HEAT = 3,
    FAN = 4,
  };

  // Logical fan speeds. The wire code (byte 5 bits 6-7) is inverted: auto=0, but
  // high=1, med=2, low=3. The enum values are the logical order; the byte stores
  // the inverted code (mirrors IRHaierAC::setFan/getFan).
  enum class Fan : uint8_t
  {
    AUTO = 0,
    LOW_SPEED = 1,
    MED_SPEED = 2,
    HIGH_SPEED = 3,
  };

  // Vertical swing (byte 2 bits 6-7).
  enum class SwingV : uint8_t
  {
    OFF = 0,
    UP = 1,
    DOWN = 2,
    CYCLE = 3, // "changing" swing; CHANGE is an Arduino macro, so CYCLE here
  };

  inline const char *toString(Mode m)
  {
    switch (m)
    {
    case Mode::AUTO: return "AUTO";
    case Mode::COOL: return "COOL";
    case Mode::DRY: return "DRY";
    case Mode::HEAT: return "HEAT";
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
  inline const char *toString(SwingV s)
  {
    switch (s)
    {
    case SwingV::OFF: return "OFF";
    case SwingV::UP: return "UP";
    case SwingV::DOWN: return "DOWN";
    case SwingV::CYCLE: return "CYCLE";
    }
    return "?";
  }

  namespace detail
  {
    // Standard HAIER_AC timing. MSB-first. The main header is 3000/4300; a separate
    // 3000/3000 pre-header precedes it (handled by the custom toRaw/fromRaw).
    inline constexpr AcTiming kTiming = {
        /*header_mark_us=*/3000,
        /*header_space_us=*/4300,
        /*bit_mark_us=*/520,
        /*zero_space_us=*/650,
        /*one_space_us=*/1650,
        /*trailer_mark_us=*/520,
        /*frame_gap_us=*/20000, // sent once; real gap (150ms) is never captured
        /*tol_pct=*/40,         // wide decode window (see [[ac-decode-tolerance-loose]])
        /*lsb_first=*/false,    // MSB-first
    };

    inline constexpr size_t kBytes = 9;
    inline constexpr size_t kBits = 72;
    inline constexpr uint32_t kPreHeaderUs = 3000; // leading mark + equal space

    inline constexpr uint8_t kPrefix = 0xA5; // 0b10100101
    inline constexpr uint8_t kUnknownBit = 0x20; // byte 2 bit 5, const 1

    inline constexpr size_t kOffCmdTemp = 1; // Command bits0-3, Temp bits4-7
    inline constexpr size_t kOffSwing = 2;   // unknown bit5, SwingV bits6-7
    inline constexpr size_t kOffFan = 5;     // Fan bits6-7 (inverted wire code)
    inline constexpr size_t kOffMode = 6;    // Mode bits5-7
    inline constexpr size_t kOffSum = 8;

    inline constexpr uint8_t kCmdOff = 0b0000;
    inline constexpr uint8_t kCmdOn = 0b0001;
    inline constexpr uint8_t kMinTempC = 16;
    inline constexpr uint8_t kMaxTempC = 30;

    inline uint8_t calcChecksum(const uint8_t *b)
    {
      uint8_t sum = 0;
      for (size_t i = 0; i < kBytes - 1; ++i)
        sum = static_cast<uint8_t>(sum + b[i]);
      return sum;
    }
    inline void writeChecksum(uint8_t *b) { b[kOffSum] = calcChecksum(b); }
    inline bool checksumOk(const uint8_t *b) { return b[kOffSum] == calcChecksum(b); }

    // Fan <-> wire code (byte 5 bits 6-7): auto=0, high=1, med=2, low=3.
    inline uint8_t fanToWire(Fan f)
    {
      switch (f)
      {
      case Fan::HIGH_SPEED: return 1;
      case Fan::MED_SPEED: return 2;
      case Fan::LOW_SPEED: return 3;
      default: return 0; // AUTO
      }
    }
    inline Fan wireToFan(uint8_t c)
    {
      switch (c & 0x3u)
      {
      case 1: return Fan::HIGH_SPEED;
      case 2: return Fan::MED_SPEED;
      case 3: return Fan::LOW_SPEED;
      default: return Fan::AUTO;
      }
    }
    inline Mode codeToMode(uint8_t c)
    {
      switch (c & 0x7u)
      {
      case 1: return Mode::COOL;
      case 2: return Mode::DRY;
      case 3: return Mode::HEAT;
      case 4: return Mode::FAN;
      default: return Mode::AUTO;
      }
    }
  } // namespace detail

  struct Frame
  {
    static constexpr size_t kBytes = detail::kBytes; // 9
    // 4 leading header entries + 72 bits + trailer + gap
    static constexpr size_t kMaxTicks = 4 + 72 * 2 + 2 + 6;

    // raw state: a known-good frame (power on / cool / 24C / fan auto / swing off),
    // OffHours=12 as IRHaierAC's reset state, checksum precomputed.
    uint8_t bytes[kBytes] = {0xA5, 0x81, 0x20, 0x00, 0x0C, 0x00, 0x20, 0x00, 0x72};
    uint16_t byte_length = 0;
    bool checksum_ok = false;

    uint8_t command() const { return bytes[detail::kOffCmdTemp] & 0x0Fu; }
    bool power() const { return command() != detail::kCmdOff; }
    void setPower(bool on)
    {
      const uint8_t cmd = on ? detail::kCmdOn : detail::kCmdOff;
      bytes[detail::kOffCmdTemp] =
          static_cast<uint8_t>((bytes[detail::kOffCmdTemp] & 0xF0u) | cmd);
    }

    Mode mode() const { return detail::codeToMode((bytes[detail::kOffMode] >> 5) & 0x07u); }
    void setMode(Mode m)
    {
      bytes[detail::kOffMode] = static_cast<uint8_t>(
          (bytes[detail::kOffMode] & ~(0x07u << 5)) | ((static_cast<uint8_t>(m) & 0x07u) << 5));
    }

    Fan fan() const { return detail::wireToFan((bytes[detail::kOffFan] >> 6) & 0x03u); }
    void setFan(Fan f)
    {
      bytes[detail::kOffFan] = static_cast<uint8_t>(
          (bytes[detail::kOffFan] & ~(0x03u << 6)) | ((detail::fanToWire(f) & 0x03u) << 6));
    }

    uint8_t temperatureC() const
    {
      return static_cast<uint8_t>(((bytes[detail::kOffCmdTemp] >> 4) & 0x0Fu) + detail::kMinTempC);
    }
    void setTemperatureC(uint8_t c)
    {
      if (c < detail::kMinTempC) c = detail::kMinTempC;
      if (c > detail::kMaxTempC) c = detail::kMaxTempC;
      bytes[detail::kOffCmdTemp] = static_cast<uint8_t>(
          (bytes[detail::kOffCmdTemp] & 0x0Fu) | (((c - detail::kMinTempC) & 0x0Fu) << 4));
    }

    SwingV swingV() const
    {
      return static_cast<SwingV>((bytes[detail::kOffSwing] >> 6) & 0x03u);
    }
    void setSwingV(SwingV s)
    {
      bytes[detail::kOffSwing] = static_cast<uint8_t>(
          (bytes[detail::kOffSwing] & ~(0x03u << 6)) | ((static_cast<uint8_t>(s) & 0x03u) << 6));
    }

    void printTo(Print &out) const
    {
      printAcSummary(out, "Haier", power(), toString(mode()),
                     static_cast<float>(temperatureC()), toString(fan()), bytes,
                     byte_length, checksum_ok);
      out.print("// swingV=");
      out.println(toString(swingV()));
    }

    void printSetterSnippet(Print &out) const
    {
      out.println("// send code (Haier AC, editable -- lossy: timers/sleep/health use defaults):");
      out.println("esp32irpk::ac::Haier::Frame f;");
      out.print("f.setPower(");
      out.print(power() ? "true" : "false");
      out.println(");");
      out.print("f.setMode(esp32irpk::ac::Haier::Mode::");
      out.print(toString(mode()));
      out.println(");");
      out.print("f.setTemperatureC(");
      out.print((unsigned)temperatureC());
      out.println(");");
      out.print("f.setFan(esp32irpk::ac::Haier::Fan::");
      out.print(toString(fan()));
      out.println(");");
      out.print("f.setSwingV(esp32irpk::ac::Haier::SwingV::");
      out.print(toString(swingV()));
      out.println(");");
      out.println("esp32irpk::ac::send(tx, f);");
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

    // RAW ticks -> state bytes. Scans for the Haier double header (3000/3000
    // pre-header + 3000/4300 header), reads 72 MSB-first bits, and gates on the
    // fixed 0xA5 prefix. Checksum validity is reported via `checksum_ok`.
    static bool fromRaw(const esp32irpk::IRRawTickView &raw, Frame &out)
    {
      out = Frame{};
      if (!raw.ticks)
        return false;
      const AcTiming &t = detail::kTiming;
      namespace d = esp32irpk::ac::detail;
      const uint32_t one_threshold_us = (t.zero_space_us + t.one_space_us) / 2;
      auto us = [&](size_t i) { return static_cast<uint32_t>(raw.ticks[i]) * d::kTickUs; };

      for (size_t start = 0; start + 4 < raw.len; ++start)
      {
        // Double header: pre-header 3000/3000, then main header 3000/4300.
        if (!d::within(us(start), detail::kPreHeaderUs, t.tol_pct) ||
            !d::within(us(start + 1), detail::kPreHeaderUs, t.tol_pct) ||
            !d::within(us(start + 2), t.header_mark_us, t.tol_pct) ||
            !d::within(us(start + 3), t.header_space_us, t.tol_pct))
          continue;

        uint8_t b[detail::kBytes] = {};
        size_t pos = start + 4;
        bool ok = true;
        for (size_t i = 0; i < detail::kBits; ++i)
        {
          if (pos + 1 >= raw.len)
          {
            ok = false;
            break;
          }
          if (!d::within(us(pos), t.bit_mark_us, t.tol_pct))
          {
            ok = false;
            break;
          }
          if (us(pos + 1) >= one_threshold_us)
            b[i / 8] |= static_cast<uint8_t>(1u << (7 - (i % 8))); // MSB-first
          pos += 2;
        }
        if (!ok || b[0] != detail::kPrefix)
          continue;

        for (size_t i = 0; i < detail::kBytes; ++i)
          out.bytes[i] = b[i];
        out.byte_length = kBytes;
        out.checksum_ok = detail::checksumOk(out.bytes);
        return true;
      }
      return false;
    }

    // state -> RAW ticks. Forces the prefix and the const bit, recomputes the sum
    // checksum, then renders the double header + 72 MSB-first bits + trailer once
    // (HAIER_AC is sent once, kHaierAcDefaultRepeat == kNoRepeat).
    bool toRaw(esp32irpk::IRRawTickBuffer &out) const
    {
      uint8_t buf[kBytes];
      for (size_t i = 0; i < kBytes; ++i)
        buf[i] = bytes[i];
      buf[0] = detail::kPrefix;
      buf[detail::kOffSwing] |= detail::kUnknownBit;
      detail::writeChecksum(buf);

      const AcTiming &t = detail::kTiming;
      namespace d = esp32irpk::ac::detail;
      out.len = 0;
      auto push = [&](uint16_t tick) -> bool {
        if (out.len >= out.capacity)
          return false;
        out.ticks[out.len++] = tick;
        return true;
      };

      // Double header: pre-header 3000/3000, main header 3000/4300.
      if (!push(d::usToTicks(detail::kPreHeaderUs)) || !push(d::usToTicks(detail::kPreHeaderUs)) ||
          !push(d::usToTicks(t.header_mark_us)) || !push(d::usToTicks(t.header_space_us)))
        return false;
      for (size_t i = 0; i < detail::kBits; ++i)
      {
        const bool one = (buf[i / 8] >> (7 - (i % 8))) & 0x1u; // MSB-first
        if (!push(d::usToTicks(t.bit_mark_us)) ||
            !push(d::usToTicks(one ? t.one_space_us : t.zero_space_us)))
          return false;
      }
      return push(d::usToTicks(t.trailer_mark_us)) && push(d::usToTicks(t.frame_gap_us));
    }
  };

} // namespace esp32irpk::ac::Haier
