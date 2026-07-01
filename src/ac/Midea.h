#pragma once

#include "../ESP32IRPulseKit.h"
#include "AcCodec.h"

// Midea air-conditioner support (the 48-bit / 6-byte Midea protocol, used by many
// OEM-rebadged brands: Pioneer, Comfee, Kaysun, Keystone, MrCool, Danby, Trotec,
// Lennox, ...). One message is sent as TWO transmissions: the 48 data bits, then
// the same 48 bits fully bit-inverted. MSB-first. See SPEC 11.2.
//
// Byte layout is transmission order (byte 0 is the first byte on the wire = the
// most-significant byte of IRremoteESP8266's 48-bit `remote_state`):
//   byte 0: Type (bits 0-2) + Header (bits 3-7, fixed 0b10100)   -> 0xA1 command
//   byte 1: Mode (bits 0-2) + Fan (bits 3-4) + Sleep (bit 6) + Power (bit 7)
//   byte 2: Temp (bits 0-4, degC-17) + useFahrenheit (bit 5)
//   byte 3: OffTimer / beep (0xFF = timer off)
//   byte 4: SensorTemp / disableSensor (0xFF = sensor off)
//   byte 5: checksum (negated sum of the bit-reversed other five bytes)
// This is the reverse byte order of IRremoteESP8266's `remote_state` union, so its
// byte 0 (the checksum) is our byte 5. Checksum arithmetic is order-independent.
//
// The double-inverted structure does not fit the plain pulse-distance codec, so
// toRaw/fromRaw render/scan it directly. Single-format vendor (no model axis).
// Celsius only (useFahrenheit forced clear). Sleep, timers, sensor/follow-me and
// the special toggle messages (swing/econo/turbo/light/clean/8C-heat/quiet) are
// documented but not settable here. Verified field-for-field against
// IRremoteESP8266's IRMideaAC via the compat_matrix_ac studies.

namespace esp32irpk::ac::Midea
{

  // Per-vendor enums; values are the wire codes (Mode: byte 1 bits 0-2).
  enum class Mode : uint8_t
  {
    COOL = 0,
    DRY = 1,
    AUTO = 2,
    HEAT = 3,
    FAN = 4,
  };

  // Fan lives in byte 1 bits 3-4 (only auto/low/med/high exist; no min/max).
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
    case Mode::COOL: return "COOL";
    case Mode::DRY: return "DRY";
    case Mode::AUTO: return "AUTO";
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

  namespace detail
  {
    // Standard Midea timing (kMideaTick = 80us). MSB-first. Note bit_mark and
    // zero_space are equal (560us); a 1 bit is the long 1680us space.
    inline constexpr AcTiming kTiming = {
        /*header_mark_us=*/4480,
        /*header_space_us=*/4480,
        /*bit_mark_us=*/560,
        /*zero_space_us=*/560,
        /*one_space_us=*/1680,
        /*trailer_mark_us=*/560,
        /*frame_gap_us=*/5600, // kMideaMinGap, separates the two copies
        /*tol_pct=*/40,        // wide decode window (see [[ac-decode-tolerance-loose]])
        /*lsb_first=*/false,   // MSB-first (custom loops below honor this directly)
    };

    inline constexpr size_t kBytes = 6;
    inline constexpr size_t kBits = 48;
    inline constexpr uint32_t kGapUs = 5600; // inter-copy gap (kMideaMinGap)

    inline constexpr uint8_t kHeaderField = 0b10100; // byte 0 bits 3-7
    inline constexpr uint8_t kHeaderMask = 0xF8;     // top 5 bits
    inline constexpr uint8_t kByte0Command = 0xA1;   // Header 0b10100 + Type command 0b001

    inline constexpr size_t kOffModeFan = 1; // Mode bits0-2, Fan bits3-4, Sleep bit6, Power bit7
    inline constexpr size_t kOffTemp = 2;    // Temp bits0-4, useFahrenheit bit5
    inline constexpr size_t kOffSum = 5;     // checksum

    inline constexpr uint8_t kPowerMask = 0x80;    // byte 1 bit 7
    inline constexpr uint8_t kFahrenheitMask = 0x20; // byte 2 bit 5
    inline constexpr uint8_t kMinTempC = 17;
    inline constexpr uint8_t kMaxTempC = 30;
    inline constexpr uint8_t kFanHigh = 3;

    inline uint8_t reverseBits8(uint8_t b)
    {
      b = static_cast<uint8_t>(((b & 0xF0u) >> 4) | ((b & 0x0Fu) << 4));
      b = static_cast<uint8_t>(((b & 0xCCu) >> 2) | ((b & 0x33u) << 2));
      b = static_cast<uint8_t>(((b & 0xAAu) >> 1) | ((b & 0x55u) << 1));
      return b;
    }

    // Checksum (IRMideaAC::calcChecksum): sum the bit-reversed value of the five
    // non-checksum bytes, negate mod 256, then bit-reverse the result. Byte order
    // is irrelevant to the sum, so our bytes 0-4 == the union's bytes 1-5.
    inline uint8_t calcChecksum(const uint8_t *b)
    {
      uint8_t sum = 0;
      for (uint8_t i = 0; i < 5; ++i)
        sum = static_cast<uint8_t>(sum + reverseBits8(b[i]));
      sum = static_cast<uint8_t>(256u - sum);
      return reverseBits8(sum);
    }
    inline void writeChecksum(uint8_t *b) { b[kOffSum] = calcChecksum(b); }
    inline bool checksumOk(const uint8_t *b) { return b[kOffSum] == calcChecksum(b); }

    inline Mode codeToMode(uint8_t c)
    {
      switch (c & 0x7u)
      {
      case 0: return Mode::COOL;
      case 1: return Mode::DRY;
      case 3: return Mode::HEAT;
      case 4: return Mode::FAN;
      default: return Mode::AUTO; // 2 (and any spare code)
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
    static constexpr size_t kBytes = detail::kBytes; // 6
    // 2 copies x (header 2 + 48 bits*2 + trailer 2)
    static constexpr size_t kMaxTicks = 2 * (2 + detail::kBits * 2 + 2) + 8;

    // raw state: a known-good command frame (on / cool / 24C / fan auto, Celsius)
    // with the checksum precomputed. A1 80 07 FF FF 39.
    uint8_t bytes[kBytes] = {0xA1, 0x80, 0x07, 0xFF, 0xFF, 0x39};
    uint16_t byte_length = 0;
    bool checksum_ok = false;

    bool power() const { return (bytes[detail::kOffModeFan] & detail::kPowerMask) != 0; }
    void setPower(bool on)
    {
      if (on)
        bytes[detail::kOffModeFan] |= detail::kPowerMask;
      else
        bytes[detail::kOffModeFan] &= static_cast<uint8_t>(~detail::kPowerMask);
    }

    Mode mode() const { return detail::codeToMode(bytes[detail::kOffModeFan]); }
    void setMode(Mode m)
    {
      const uint8_t code = static_cast<uint8_t>(m) & 0x7u;
      bytes[detail::kOffModeFan] =
          static_cast<uint8_t>((bytes[detail::kOffModeFan] & ~0x07u) | code);
    }

    Fan fan() const { return detail::codeToFan((bytes[detail::kOffModeFan] >> 3) & 0x03u); }
    void setFan(Fan f)
    {
      uint8_t speed = static_cast<uint8_t>(f);
      if (speed > detail::kFanHigh)
        speed = 0; // IRMideaAC maps out-of-range to auto
      bytes[detail::kOffModeFan] = static_cast<uint8_t>(
          (bytes[detail::kOffModeFan] & ~(0x03u << 3)) | ((speed & 0x03u) << 3));
    }

    // Celsius temperature (useFahrenheit is forced clear on encode).
    uint8_t temperatureC() const
    {
      return static_cast<uint8_t>((bytes[detail::kOffTemp] & 0x1Fu) + detail::kMinTempC);
    }
    void setTemperatureC(uint8_t c)
    {
      if (c < detail::kMinTempC) c = detail::kMinTempC;
      if (c > detail::kMaxTempC) c = detail::kMaxTempC;
      bytes[detail::kOffTemp] = static_cast<uint8_t>((c - detail::kMinTempC) & 0x1Fu);
    }

    void printTo(Print &out) const
    {
      printAcSummary(out, "Midea", power(), toString(mode()),
                     static_cast<float>(temperatureC()), toString(fan()), bytes,
                     byte_length, checksum_ok);
    }

    void printSetterSnippet(Print &out) const
    {
      out.println("// send code (Midea AC, editable -- lossy: sleep/timers/toggles use defaults):");
      out.println("esp32irpk::ac::Midea::Frame f;");
      out.print("f.setPower(");
      out.print(power() ? "true" : "false");
      out.println(");");
      out.print("f.setMode(esp32irpk::ac::Midea::Mode::");
      out.print(toString(mode()));
      out.println(");");
      out.print("f.setTemperatureC(");
      out.print((unsigned)temperatureC());
      out.println(");");
      out.print("f.setFan(esp32irpk::ac::Midea::Fan::");
      out.print(toString(fan()));
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

    // RAW ticks -> state bytes. Scans for the Midea double transmission (header +
    // 48 bits, then header + the same 48 bits inverted); false unless both copies
    // decode, the second is the exact bit-complement of the first, and the fixed
    // Header field (byte 0 bits 3-7 = 0b10100) matches. Checksum validity is
    // reported via `checksum_ok`.
    static bool fromRaw(const esp32irpk::IRRawTickView &raw, Frame &out)
    {
      out = Frame{};
      if (!raw.ticks)
        return false;
      const AcTiming &t = detail::kTiming;
      namespace d = esp32irpk::ac::detail;
      const uint32_t one_threshold_us = (t.zero_space_us + t.one_space_us) / 2;

      // Read 48 MSB-first pulse-distance bits into `b` (6 bytes); advance `pos`.
      auto readCopy = [&](size_t &pos, uint8_t *b) -> bool {
        for (size_t i = 0; i < detail::kBits; ++i)
        {
          if (pos + 1 >= raw.len)
            return false;
          if (!d::within(static_cast<uint32_t>(raw.ticks[pos]) * d::kTickUs, t.bit_mark_us, t.tol_pct))
            return false;
          const uint32_t space_us = static_cast<uint32_t>(raw.ticks[pos + 1]) * d::kTickUs;
          if (space_us >= one_threshold_us)
            b[i / 8] |= static_cast<uint8_t>(1u << (7 - (i % 8))); // MSB-first
          pos += 2;
        }
        return true;
      };
      // Consume a trailer mark + its following gap. The gap after the very last
      // copy is absent when this is the final frame of a capture (the RX does not
      // record the trailing idle space), so accept a trailer mark with no gap.
      auto consumeTrailer = [&](size_t &pos) -> bool {
        if (pos >= raw.len)
          return false;
        if (!d::within(static_cast<uint32_t>(raw.ticks[pos]) * d::kTickUs, t.bit_mark_us, t.tol_pct))
          return false;
        pos += (pos + 1 < raw.len) ? 2 : 1;
        return true;
      };
      auto isHeader = [&](size_t p) -> bool {
        return p + 1 < raw.len &&
               d::within(static_cast<uint32_t>(raw.ticks[p]) * d::kTickUs, t.header_mark_us, t.tol_pct) &&
               d::within(static_cast<uint32_t>(raw.ticks[p + 1]) * d::kTickUs, t.header_space_us, t.tol_pct);
      };

      for (size_t start = 0; start + 1 < raw.len; ++start)
      {
        if (!isHeader(start))
          continue;
        size_t pos = start + 2;
        uint8_t c1[detail::kBytes] = {};
        if (!readCopy(pos, c1) || !consumeTrailer(pos))
          continue;
        if (!isHeader(pos))
          continue;
        pos += 2;
        uint8_t c2[detail::kBytes] = {};
        if (!readCopy(pos, c2) || !consumeTrailer(pos))
          continue;
        // The second copy must be the exact bit-complement of the first.
        bool inverted_ok = true;
        for (size_t i = 0; i < detail::kBytes; ++i)
          if (c2[i] != static_cast<uint8_t>(~c1[i]))
            inverted_ok = false;
        if (!inverted_ok)
          continue;
        // Vendor signature: the fixed Header field.
        if ((c1[0] & detail::kHeaderMask) != (detail::kHeaderField << 3))
          continue;

        for (size_t i = 0; i < detail::kBytes; ++i)
          out.bytes[i] = c1[i];
        out.byte_length = kBytes;
        out.checksum_ok = detail::checksumOk(out.bytes);
        return true;
      }
      return false;
    }

    // state -> RAW ticks. Forces the command Header/Type byte and Celsius, writes
    // the checksum, then renders the two copies (header + 48 bits MSB-first +
    // trailer + gap; second copy fully bit-inverted).
    bool toRaw(esp32irpk::IRRawTickBuffer &out) const
    {
      uint8_t buf[kBytes];
      for (size_t i = 0; i < kBytes; ++i)
        buf[i] = bytes[i];
      buf[0] = detail::kByte0Command;               // fixed Header + command Type
      buf[detail::kOffTemp] &= static_cast<uint8_t>(~detail::kFahrenheitMask); // Celsius
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

      for (uint8_t copy = 0; copy < 2; ++copy)
      {
        if (!push(d::usToTicks(t.header_mark_us)) || !push(d::usToTicks(t.header_space_us)))
          return false;
        for (size_t i = 0; i < detail::kBits; ++i)
        {
          const uint8_t byte = (copy == 0) ? buf[i / 8] : static_cast<uint8_t>(~buf[i / 8]);
          const bool one = (byte >> (7 - (i % 8))) & 0x1u; // MSB-first
          if (!push(d::usToTicks(t.bit_mark_us)) ||
              !push(d::usToTicks(one ? t.one_space_us : t.zero_space_us)))
            return false;
        }
        if (!push(d::usToTicks(t.trailer_mark_us)) || !push(d::usToTicks(detail::kGapUs)))
          return false;
      }
      return true;
    }
  };

} // namespace esp32irpk::ac::Midea
