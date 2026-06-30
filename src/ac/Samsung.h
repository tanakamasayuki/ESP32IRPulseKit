#pragma once

#include "../ESP32IRPulseKit.h"
#include "AcCodec.h"

// Samsung air-conditioner support (the standard 14-byte SAMSUNG_AC protocol used
// by the AR-/ARH- series remotes). A setting is a 14-byte state sent LSB-first as
// a one-time leading header (690us mark + 17844us space) followed by two 7-byte
// sections, each with its own section header (3086/8864us) and a 2886us section
// gap. Each section carries a popcount (Hamming-weight) checksum split across two
// nibbles of its bytes 1-2. See SPEC §11.2.
//
// Frame mechanics (timing, section framing, popcount checksum) follow the
// documented format. The logical field map (byte/bit positions and the mode/fan
// codes) is verified field-for-field against IRremoteESP8266's IRSamsungAc via the
// compat_matrix_ac studies (samsung_irremoteesp8266_*).
//
// This targets the standard 14-byte format. The 21-byte "extended" message (timer
// programming) is a different frame and would be a separate Frame type (SPEC
// §11.2). Swing and the special fan flags (Powerful/WindFree/Econo) are not
// settable fields here; the standard power/mode/temperature/fan map is modelled.
//
// Power is two 2-bit fields (byte 6 and byte 13): both 0b11 == on, both 0b00 ==
// off.

namespace esp32irpk::ac::Samsung
{

  // Per-vendor enums; values are the wire codes. Mode lives in byte 12 bits 4-6.
  enum class Mode : uint8_t
  {
    AUTO = 0,
    COOL = 1,
    DRY = 2,
    FAN = 3,
    HEAT = 4,
  };

  // Fan lives in byte 12 bits 1-3. The wire codes are non-contiguous: auto=0, then
  // 2 (low), 4 (med), 5 (high), 7 (turbo/max). Enum values ARE the wire codes.
  enum class Fan : uint8_t
  {
    AUTO = 0,
    LOW_SPEED = 2,
    MED_SPEED = 4,
    HIGH_SPEED = 5,
    MAX_SPEED = 7,
  };

  inline const char *toString(Mode m)
  {
    switch (m)
    {
    case Mode::AUTO: return "AUTO";
    case Mode::COOL: return "COOL";
    case Mode::DRY: return "DRY";
    case Mode::FAN: return "FAN";
    case Mode::HEAT: return "HEAT";
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
    case Fan::MAX_SPEED: return "MAX_SPEED";
    }
    return "?";
  }

  namespace detail
  {
    // Standard SAMSUNG_AC section timing. LSB-first. header_mark/space are the
    // per-section header (3086/8864); frame_gap is the inter-section gap (2886).
    // The one-time leading header (690/17844) is rendered inline by toRaw and
    // skipped by fromRaw's scan. The section decoder uses no mark-excess, so the
    // standard 436us zero-space clears IRremoteESP8266's window with margin (unlike
    // Toshiba, which needed a shortened emit).
    inline constexpr AcTiming kTiming = {
        /*header_mark_us=*/3086,
        /*header_space_us=*/8864,
        /*bit_mark_us=*/586,
        /*zero_space_us=*/436,
        /*one_space_us=*/1432,
        /*trailer_mark_us=*/586,
        /*frame_gap_us=*/2886,
        /*tol_pct=*/30,
        /*lsb_first=*/true,
    };

    // The one-time leading header that precedes the two sections.
    inline constexpr uint16_t kLeadMark = 690;
    inline constexpr uint16_t kLeadSpace = 17844;

    inline constexpr size_t kBytes = 14;
    inline constexpr size_t kSectionLen = 7; // two sections of 7 bytes

    // Field offsets (byte indices into the 14-byte state).
    inline constexpr size_t kOffPower1 = 6;  // bits 4-5 (0b11 on / 0b00 off)
    inline constexpr size_t kOffPower2 = 13; // bits 4-5
    inline constexpr size_t kOffTemp = 11;   // bits 4-7, value = degC - 16
    inline constexpr size_t kOffModeFan = 12; // Fan bits 1-3, Mode bits 4-6
    inline constexpr uint8_t kMinTempC = 16;
    inline constexpr uint8_t kMaxTempC = 30;

    inline uint8_t popcount8(uint8_t v)
    {
      uint8_t n = 0;
      while (v) { n += (v & 1u); v >>= 1; }
      return n;
    }

    // Popcount checksum of one 7-byte section: count set bits over byte0, the low
    // nibble of byte1, the high nibble of byte2, and bytes 3-6, then bitwise-invert.
    // (The two checksum nibbles themselves — byte1 high, byte2 low — are excluded.)
    inline uint8_t calcSectionChecksum(const uint8_t *s)
    {
      uint8_t sum = 0;
      sum += popcount8(s[0]);
      sum += popcount8(static_cast<uint8_t>(s[1] & 0x0Fu));
      sum += popcount8(static_cast<uint8_t>((s[2] >> 4) & 0x0Fu));
      sum += popcount8(s[3]);
      sum += popcount8(s[4]);
      sum += popcount8(s[5]);
      sum += popcount8(s[6]);
      return static_cast<uint8_t>(sum ^ 0xFFu);
    }
    // Stored checksum: low nibble of byte2 is the high nibble, high nibble of byte1
    // is the low nibble.
    inline uint8_t getSectionChecksum(const uint8_t *s)
    {
      return static_cast<uint8_t>(((s[2] & 0x0Fu) << 4) | ((s[1] >> 4) & 0x0Fu));
    }
    inline void writeSectionChecksum(uint8_t *s)
    {
      const uint8_t c = calcSectionChecksum(s);
      s[1] = static_cast<uint8_t>((s[1] & 0x0Fu) | ((c & 0x0Fu) << 4));
      s[2] = static_cast<uint8_t>((s[2] & 0xF0u) | ((c >> 4) & 0x0Fu));
    }
    inline void writeChecksums(uint8_t *b)
    {
      writeSectionChecksum(b);                 // section 1: bytes 0-6
      writeSectionChecksum(b + kSectionLen);   // section 2: bytes 7-13
    }
    inline bool checksumOk(const uint8_t *b)
    {
      return getSectionChecksum(b) == calcSectionChecksum(b) &&
             getSectionChecksum(b + kSectionLen) == calcSectionChecksum(b + kSectionLen);
    }

    inline Mode codeToMode(uint8_t c)
    {
      switch (c)
      {
      case 1: return Mode::COOL;
      case 2: return Mode::DRY;
      case 3: return Mode::FAN;
      case 4: return Mode::HEAT;
      default: return Mode::AUTO;
      }
    }
    inline Fan codeToFan(uint8_t c)
    {
      switch (c)
      {
      case 2: return Fan::LOW_SPEED;
      case 4: return Fan::MED_SPEED;
      case 5: return Fan::HIGH_SPEED;
      case 7: return Fan::MAX_SPEED;
      default: return Fan::AUTO; // 0, alt-auto 6, unused 1/3, and any unknown
      }
    }
  } // namespace detail

  struct Frame
  {
    static constexpr size_t kBytes = detail::kBytes; // 14
    // leading header (2) + two sections (2 + 112 + 2 each) = 234.
    static constexpr size_t kMaxTicks = 260;

    // raw state: IRSamsungAc's reset state (power on / Cool / 16C / fan low),
    // section checksums precomputed.
    uint8_t bytes[kBytes] = {0x02, 0x92, 0x0F, 0x00, 0x00, 0x00, 0xF0,
                             0x01, 0x02, 0xAE, 0x71, 0x00, 0x15, 0xF0};
    uint16_t byte_length = 0;
    bool checksum_ok = false;

    bool power() const
    {
      return ((bytes[detail::kOffPower1] >> 4) & 0x3u) == 0x3u &&
             ((bytes[detail::kOffPower2] >> 4) & 0x3u) == 0x3u;
    }
    void setPower(bool on)
    {
      const uint8_t v = on ? 0x3u : 0x0u;
      bytes[detail::kOffPower1] =
          static_cast<uint8_t>((bytes[detail::kOffPower1] & ~(0x3u << 4)) | (v << 4));
      bytes[detail::kOffPower2] =
          static_cast<uint8_t>((bytes[detail::kOffPower2] & ~(0x3u << 4)) | (v << 4));
    }

    Mode mode() const
    {
      return detail::codeToMode((bytes[detail::kOffModeFan] >> 4) & 0x07u);
    }
    void setMode(Mode m)
    {
      bytes[detail::kOffModeFan] = static_cast<uint8_t>(
          (bytes[detail::kOffModeFan] & ~(0x07u << 4)) | ((static_cast<uint8_t>(m) & 0x07u) << 4));
    }

    // Whole-degree temperature, byte 11 high nibble = °C - 16, clamped 16..30.
    uint8_t temperatureC() const
    {
      return static_cast<uint8_t>(((bytes[detail::kOffTemp] >> 4) & 0x0Fu) + detail::kMinTempC);
    }
    void setTemperatureC(uint8_t c)
    {
      if (c < detail::kMinTempC) c = detail::kMinTempC;
      if (c > detail::kMaxTempC) c = detail::kMaxTempC;
      bytes[detail::kOffTemp] = static_cast<uint8_t>(
          (bytes[detail::kOffTemp] & 0x0Fu) | ((c - detail::kMinTempC) << 4));
    }

    Fan fan() const { return detail::codeToFan((bytes[detail::kOffModeFan] >> 1) & 0x07u); }
    void setFan(Fan f)
    {
      bytes[detail::kOffModeFan] = static_cast<uint8_t>(
          (bytes[detail::kOffModeFan] & ~(0x07u << 1)) | ((static_cast<uint8_t>(f) & 0x07u) << 1));
    }

    void printTo(Print &out) const
    {
      printAcSummary(out, "Samsung", power(), toString(mode()),
                     static_cast<float>(temperatureC()), toString(fan()), bytes,
                     byte_length, checksum_ok);
    }

    // Editable setter template: copy-paste C++ that rebuilds this frame through the
    // logical setters. Lossy (swing and the special fan flags are not modelled
    // here), so it is not bit-exact (use fromBytes for that).
    void printSetterSnippet(Print &out) const
    {
      out.println("// send code (Samsung AC, editable -- lossy: swing/special-fan use defaults):");
      out.println("esp32irpk::ac::Samsung::Frame f;");
      out.print("f.setPower(");
      out.print(power() ? "true" : "false");
      out.println(");");
      out.print("f.setMode(esp32irpk::ac::Samsung::Mode::");
      out.print(toString(mode()));
      out.println(");");
      out.print("f.setTemperatureC(");
      out.print((unsigned)temperatureC());
      out.println(");");
      out.print("f.setFan(esp32irpk::ac::Samsung::Fan::");
      out.print(toString(fan()));
      out.println(");");
      out.println("esp32irpk::ac::send(tx, f);");
    }

    // RAW ticks -> state bytes. Scans past the leading header for the first section
    // header, decodes the two 7-byte sections, and requires both section checksums
    // to validate (Samsung carries no fixed signature, so the popcount checksum is
    // the structural gate). LSB-first.
    static bool fromRaw(const esp32irpk::IRRawTickView &raw, Frame &out)
    {
      out = Frame{};
      if (!raw.ticks)
        return false;
      const AcTiming &t = detail::kTiming;

      for (size_t start = 0; start + 1 < raw.len; ++start)
      {
        // A section header is a ~3086us mark + ~8864us space. The leading header
        // (690/17844) and the section bits (~586us) do not match, so this lands on
        // a real section header.
        if (!esp32irpk::ac::detail::within(
                static_cast<uint32_t>(raw.ticks[start]) * esp32irpk::ac::detail::kTickUs,
                t.header_mark_us, t.tol_pct) ||
            !esp32irpk::ac::detail::within(
                static_cast<uint32_t>(raw.ticks[start + 1]) * esp32irpk::ac::detail::kTickUs,
                t.header_space_us, t.tol_pct))
          continue;

        size_t pos = start;
        uint8_t b1[detail::kSectionLen] = {};
        uint8_t b2[detail::kSectionLen] = {};
        if (rawFrameToBytes(raw, pos, t, b1, sizeof(b1)) != detail::kSectionLen * 8)
          continue;
        if (rawFrameToBytes(raw, pos, t, b2, sizeof(b2)) != detail::kSectionLen * 8)
          continue;

        uint8_t b[detail::kBytes] = {};
        for (size_t i = 0; i < detail::kSectionLen; ++i) b[i] = b1[i];
        for (size_t i = 0; i < detail::kSectionLen; ++i) b[detail::kSectionLen + i] = b2[i];
        if (!detail::checksumOk(b))
          continue;

        for (size_t i = 0; i < detail::kBytes; ++i)
          out.bytes[i] = b[i];
        out.byte_length = kBytes;
        out.checksum_ok = true;
        return true;
      }
      return false;
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

    // state -> RAW ticks. Recomputes the two section checksums, then renders the
    // one-time leading header followed by the two 7-byte sections (each with its
    // own section header), as a real remote does. LSB-first.
    bool toRaw(esp32irpk::IRRawTickBuffer &out) const
    {
      uint8_t buf[kBytes];
      for (size_t i = 0; i < kBytes; ++i)
        buf[i] = bytes[i];
      detail::writeChecksums(buf);

      const AcTiming &t = detail::kTiming;
      out.len = 0;

      auto push = [&](uint16_t tick) -> bool {
        if (out.len >= out.capacity)
          return false;
        out.ticks[out.len++] = tick;
        return true;
      };
      // Leading header (rendered inline: it differs from the per-section header).
      if (!push(esp32irpk::ac::detail::usToTicks(detail::kLeadMark)) ||
          !push(esp32irpk::ac::detail::usToTicks(detail::kLeadSpace)))
        return false;

      // Two sections, each with its own header (7 bytes == 56 bits).
      if (!bytesFrameToRaw(buf, detail::kSectionLen * 8, t, out))
        return false;
      return bytesFrameToRaw(buf + detail::kSectionLen, detail::kSectionLen * 8, t, out);
    }
  };

} // namespace esp32irpk::ac::Samsung
