#pragma once

#include "../ESP32IRPulseKit.h"
#include "AcCodec.h"

// Daikin air-conditioner support (the classic "Daikin" / ARC433 protocol used by
// the common ARC433** / ARC466 remotes and the M-Series / FTXM-M units). The
// state is 35 bytes sent as a 5-bit "00000" leading preamble followed by THREE
// pulse-distance sections (8 + 8 + 19 bytes), each with its own header and a
// per-section sum checksum (bytes 7 / 15 / 34). Every section begins with the
// fixed signature 0x11 0xDA 0x27. See SPEC §11.2.
//
// Frame mechanics (timing, signatures, per-section checksums) follow the
// documented format. The logical field map (byte/bit positions and the
// mode/fan/swing codes) is verified field-for-field against IRremoteESP8266's
// IRDaikinESP via the compat_matrix_ac studies (daikin_irremoteesp8266_*).
//
// This targets the single classic ARC433 wire format (no `Model` parameter). The
// other Daikin formats (Daikin2 / 216 / 160 / 176 / 128 / 152 / 64 / 312) are
// different waveforms and would be separate Frame types (SPEC §11.2).
//
// Transmit with the PHASE-ALIGNED carrier (the library default): Daikin's
// zero-space (428us) EQUALS its bit mark (428us), the tightest margin of any
// supported vendor, so the free-running hardware carrier's mark wobble would push
// spaces out of an external decoder's tolerance (SPEC §11.3).

namespace esp32irpk::ac::Daikin
{

  // Per-vendor enums; values are the wire codes. Classic Daikin's mode codes are
  // non-contiguous (auto=0, dry=2, cool=3, heat=4, fan=6).
  enum class Mode : uint8_t
  {
    AUTO = 0,
    DRY = 2,
    COOL = 3,
    HEAT = 4,
    FAN = 6,
  };

  // Arduino defines LOW/HIGH as macros, so speed names carry the _SPEED suffix.
  // Daikin exposes auto, a quiet step, and five fan speeds (min..max). The wire
  // nibble is auto=0xA / quiet=0xB / 3..7 for the five speeds.
  enum class Fan : uint8_t
  {
    AUTO = 0x0A,
    QUIET = 0x0B,
    MIN_SPEED = 3,
    LOW_SPEED = 4,
    MED_SPEED = 5,
    HIGH_SPEED = 6,
    MAX_SPEED = 7,
  };

  // Enum-to-name helpers for logging/printf. Return the enumerator name; unknown
  // values return "?".
  inline const char *toString(Mode m)
  {
    switch (m)
    {
    case Mode::AUTO: return "AUTO";
    case Mode::DRY: return "DRY";
    case Mode::COOL: return "COOL";
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
    case Fan::QUIET: return "QUIET";
    case Fan::MIN_SPEED: return "MIN_SPEED";
    case Fan::LOW_SPEED: return "LOW_SPEED";
    case Fan::MED_SPEED: return "MED_SPEED";
    case Fan::HIGH_SPEED: return "HIGH_SPEED";
    case Fan::MAX_SPEED: return "MAX_SPEED";
    }
    return "?";
  }

  namespace detail
  {
    // Documented classic Daikin pulse-distance timing. NOTE: zero_space ==
    // bit_mark (both 428us), so the phase-aligned carrier is required.
    inline constexpr AcTiming kTiming = {
        /*header_mark_us=*/3650,
        /*header_space_us=*/1623,
        /*bit_mark_us=*/428,
        /*zero_space_us=*/428,
        /*one_space_us=*/1280,
        /*trailer_mark_us=*/428,
        /*frame_gap_us=*/29428, // kDaikinZeroSpace + kDaikinGap (428 + 29000)
        /*tol_pct=*/30,
        /*lsb_first=*/true,
    };

    inline constexpr size_t kBytes = 35;
    inline constexpr size_t kSec1Len = 8;  // bytes 0..7
    inline constexpr size_t kSec2Len = 8;  // bytes 8..15
    inline constexpr size_t kSec3Len = 19; // bytes 16..34
    inline constexpr size_t kPreambleBits = 5; // leading "00000"

    // Fixed 3-byte signature every section begins with.
    inline constexpr uint8_t kSig0 = 0x11;
    inline constexpr uint8_t kSig1 = 0xDA;
    inline constexpr uint8_t kSig2 = 0x27;

    // Byte offsets / masks for the logical fields (authoritative bitfield layout).
    inline constexpr size_t kOffPower = 21; // bit 0
    inline constexpr uint8_t kPowerMask = 0x01;
    inline constexpr size_t kOffMode = 21;  // bits 4-6
    inline constexpr size_t kOffTemp = 22;  // 8 bits = C * 2
    inline constexpr size_t kOffFanSwingV = 24; // low nibble SwingV, high nibble Fan
    inline constexpr size_t kOffSwingH = 25;     // low nibble SwingH
    inline constexpr uint8_t kSwingOn = 0x0F;
    inline constexpr uint8_t kSwingOff = 0x00;

    inline constexpr uint8_t kMinTempC = 10;
    inline constexpr uint8_t kMaxTempC = 32;

    // Per-section sum checksum: byte 7 = sum(0..6), byte 15 = sum(8..14),
    // byte 34 = sum(16..33). Each is the low 8 bits of the section's preceding
    // bytes.
    inline void writeChecksums(uint8_t *b)
    {
      uint16_t s1 = 0;
      for (size_t i = 0; i < 7; ++i) s1 += b[i];
      b[7] = static_cast<uint8_t>(s1 & 0xFF);
      uint16_t s2 = 0;
      for (size_t i = 8; i < 15; ++i) s2 += b[i];
      b[15] = static_cast<uint8_t>(s2 & 0xFF);
      uint16_t s3 = 0;
      for (size_t i = 16; i < 34; ++i) s3 += b[i];
      b[34] = static_cast<uint8_t>(s3 & 0xFF);
    }
    inline bool checksumOk(const uint8_t *b)
    {
      uint16_t s1 = 0;
      for (size_t i = 0; i < 7; ++i) s1 += b[i];
      uint16_t s2 = 0;
      for (size_t i = 8; i < 15; ++i) s2 += b[i];
      uint16_t s3 = 0;
      for (size_t i = 16; i < 34; ++i) s3 += b[i];
      return b[7] == static_cast<uint8_t>(s1 & 0xFF) &&
             b[15] == static_cast<uint8_t>(s2 & 0xFF) &&
             b[34] == static_cast<uint8_t>(s3 & 0xFF);
    }

    inline Mode codeToMode(uint8_t c)
    {
      switch (c)
      {
      case 2: return Mode::DRY;
      case 3: return Mode::COOL;
      case 4: return Mode::HEAT;
      case 6: return Mode::FAN;
      default: return Mode::AUTO;
      }
    }
    inline Fan codeToFan(uint8_t c)
    {
      switch (c)
      {
      case 0x0B: return Fan::QUIET;
      case 3: return Fan::MIN_SPEED;
      case 4: return Fan::LOW_SPEED;
      case 5: return Fan::MED_SPEED;
      case 6: return Fan::HIGH_SPEED;
      case 7: return Fan::MAX_SPEED;
      default: return Fan::AUTO; // 0x0A and any unknown
      }
    }
  } // namespace detail

  struct Frame
  {
    static constexpr size_t kBytes = detail::kBytes; // 35
    static constexpr size_t kMaxTicks = 640; // preamble + 3 sections

    // raw state: a known-good frame (Power on / Heat / 15C / fan quiet) matching
    // IRDaikinESP's reset state, with the three section checksums precomputed.
    // Setters touch only the logical fields; toRaw rewrites the section signatures
    // and checksums.
    uint8_t bytes[kBytes] = {
        0x11, 0xDA, 0x27, 0x00, 0xC5, 0x00, 0x00, 0xD7,
        0x11, 0xDA, 0x27, 0x00, 0x42, 0x00, 0x00, 0x54,
        0x11, 0xDA, 0x27, 0x00, 0x00, 0x49, 0x1E, 0x00, 0xB0, 0x00,
        0x00, 0x06, 0x60, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x4F};
    uint16_t byte_length = 0;
    bool checksum_ok = false;

    // Logical accessors over `bytes` (layout per the file header).
    bool power() const { return (bytes[detail::kOffPower] & detail::kPowerMask) != 0; }
    void setPower(bool on)
    {
      bytes[detail::kOffPower] = static_cast<uint8_t>(
          (bytes[detail::kOffPower] & ~detail::kPowerMask) | (on ? detail::kPowerMask : 0));
    }

    Mode mode() const { return detail::codeToMode((bytes[detail::kOffMode] >> 4) & 0x07u); }
    void setMode(Mode m)
    {
      bytes[detail::kOffMode] = static_cast<uint8_t>(
          (bytes[detail::kOffMode] & ~(0x07u << 4)) | ((static_cast<uint8_t>(m) & 0x07u) << 4));
    }

    // Temperature is stored as Celsius x 2 (byte 22), so 0.5C steps are
    // representable. Clamps to 10..32 C.
    float temperatureC() const { return bytes[detail::kOffTemp] / 2.0f; }
    void setTemperatureC(float c)
    {
      if (c < detail::kMinTempC) c = static_cast<float>(detail::kMinTempC);
      if (c > detail::kMaxTempC) c = static_cast<float>(detail::kMaxTempC);
      bytes[detail::kOffTemp] = static_cast<uint8_t>(c * 2.0f + 0.5f);
    }

    Fan fan() const { return detail::codeToFan((bytes[detail::kOffFanSwingV] >> 4) & 0x0Fu); }
    void setFan(Fan f)
    {
      bytes[detail::kOffFanSwingV] = static_cast<uint8_t>(
          (bytes[detail::kOffFanSwingV] & 0x0Fu) | ((static_cast<uint8_t>(f) & 0x0Fu) << 4));
    }

    // Vertical swing shares byte 24 (low nibble) with the fan (high nibble);
    // horizontal swing is byte 25 low nibble. Each is all-ones (0xF) for on.
    bool swingVertical() const
    {
      return (bytes[detail::kOffFanSwingV] & 0x0Fu) == detail::kSwingOn;
    }
    void setSwingVertical(bool on)
    {
      bytes[detail::kOffFanSwingV] = static_cast<uint8_t>(
          (bytes[detail::kOffFanSwingV] & 0xF0u) | (on ? detail::kSwingOn : detail::kSwingOff));
    }
    bool swingHorizontal() const
    {
      return (bytes[detail::kOffSwingH] & 0x0Fu) == detail::kSwingOn;
    }
    void setSwingHorizontal(bool on)
    {
      bytes[detail::kOffSwingH] = static_cast<uint8_t>(
          (bytes[detail::kOffSwingH] & 0xF0u) | (on ? detail::kSwingOn : detail::kSwingOff));
    }

    // Human-readable dump for diagnostics: the shared summary plus Daikin's two
    // swing flags. The hex line shows every byte.
    void printTo(Print &out) const
    {
      printAcSummary(out, "Daikin", power(), toString(mode()),
                     temperatureC(), toString(fan()), bytes,
                     byte_length, checksum_ok);
      out.print("//   swingV=");
      out.print(swingVertical() ? "on" : "off");
      out.print(" swingH=");
      out.println(swingHorizontal() ? "on" : "off");
    }

    // Editable setter template: copy-paste C++ that rebuilds this frame through
    // the logical setters, ready to tweak before sending. NOTE: lossy -- fields
    // without a setter (timers, powerful/quiet/comfort/econo/sensor/mold) reset to
    // template defaults, so it is not bit-exact (use fromBytes for that). Enums
    // come from toString.
    void printSetterSnippet(Print &out) const
    {
      out.println("// send code (Daikin AC, editable -- lossy: no-setter fields use defaults):");
      out.println("esp32irpk::ac::Daikin::Frame f;");
      out.print("f.setPower(");
      out.print(power() ? "true" : "false");
      out.println(");");
      out.print("f.setMode(esp32irpk::ac::Daikin::Mode::");
      out.print(toString(mode()));
      out.println(");");
      out.print("f.setTemperatureC(");
      out.print(temperatureC(), 1);
      out.println(");");
      out.print("f.setFan(esp32irpk::ac::Daikin::Fan::");
      out.print(toString(fan()));
      out.println(");");
      out.print("f.setSwingVertical(");
      out.print(swingVertical() ? "true" : "false");
      out.println(");");
      out.print("f.setSwingHorizontal(");
      out.print(swingHorizontal() ? "true" : "false");
      out.println(");");
      out.println("esp32irpk::ac::send(tx, f);");
    }

    // RAW ticks -> state bytes. false if not a classic Daikin frame; checksum
    // validity is reported separately via `checksum_ok`. The capture begins with a
    // 5-bit preamble (and possibly receiver noise on the short leading marks), so
    // rather than committing to the first header-like pair, this scans for a
    // start position at which all three sections (8 / 8 / 19 bytes) decode AND
    // carry the fixed 11 DA 27 signature -- skipping any false header match inside
    // the preamble.
    static bool fromRaw(const esp32irpk::IRRawTickView &raw, Frame &out)
    {
      out = Frame{};
      if (!raw.ticks)
        return false;
      const AcTiming &t = detail::kTiming;

      for (size_t start = 0; start + 1 < raw.len; ++start)
      {
        // Cheap gate: a section header is a ~3650us mark + ~1623us space. The
        // preamble's bits are short (~428us) marks, so this lands on a real header.
        if (!esp32irpk::ac::detail::within(
                static_cast<uint32_t>(raw.ticks[start]) * esp32irpk::ac::detail::kTickUs,
                t.header_mark_us, t.tol_pct) ||
            !esp32irpk::ac::detail::within(
                static_cast<uint32_t>(raw.ticks[start + 1]) * esp32irpk::ac::detail::kTickUs,
                t.header_space_us, t.tol_pct))
          continue;

        size_t pos = start;
        uint8_t b1[detail::kSec1Len] = {};
        uint8_t b2[detail::kSec2Len] = {};
        uint8_t b3[detail::kSec3Len] = {};
        if (rawFrameToBytes(raw, pos, t, b1, sizeof(b1)) != detail::kSec1Len * 8)
          continue;
        if (rawFrameToBytes(raw, pos, t, b2, sizeof(b2)) != detail::kSec2Len * 8)
          continue;
        if (rawFrameToBytes(raw, pos, t, b3, sizeof(b3)) != detail::kSec3Len * 8)
          continue;

        // Every section carries the same 3-byte signature; a false header match
        // inside the preamble fails here, so keep scanning.
        if (b1[0] != detail::kSig0 || b1[1] != detail::kSig1 || b1[2] != detail::kSig2 ||
            b2[0] != detail::kSig0 || b2[1] != detail::kSig1 || b2[2] != detail::kSig2 ||
            b3[0] != detail::kSig0 || b3[1] != detail::kSig1 || b3[2] != detail::kSig2)
          continue;

        for (size_t i = 0; i < detail::kSec1Len; ++i) out.bytes[i] = b1[i];
        for (size_t i = 0; i < detail::kSec2Len; ++i) out.bytes[detail::kSec1Len + i] = b2[i];
        for (size_t i = 0; i < detail::kSec3Len; ++i)
          out.bytes[detail::kSec1Len + detail::kSec2Len + i] = b3[i];
        out.byte_length = kBytes;
        out.checksum_ok = detail::checksumOk(out.bytes);
        return true;
      }
      return false;
    }

    // Decoded state bytes -> Frame, without going through RAW ticks. The compact,
    // bit-exact counterpart to fromRaw: copies the `kBytes` state and reports
    // checksum validity via `checksum_ok`. `len` must equal `kBytes`.
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

    // state -> RAW ticks. Rewrites the section signatures and recomputes the three
    // checksums, then renders the 5-bit "00000" preamble followed by the three
    // sections (each with its own header), as a real remote does.
    bool toRaw(esp32irpk::IRRawTickBuffer &out) const
    {
      uint8_t buf[kBytes];
      for (size_t i = 0; i < kBytes; ++i)
        buf[i] = bytes[i];
      // Force the fixed section signatures so an edited frame still renders valid.
      buf[0] = detail::kSig0;  buf[1] = detail::kSig1;  buf[2] = detail::kSig2;
      buf[8] = detail::kSig0;  buf[9] = detail::kSig1;  buf[10] = detail::kSig2;
      buf[16] = detail::kSig0; buf[17] = detail::kSig1; buf[18] = detail::kSig2;
      detail::writeChecksums(buf);

      const AcTiming &t = detail::kTiming;
      out.len = 0;

      // Leading preamble: kPreambleBits zero-bits, then a trailer mark + big gap.
      // Rendered inline because it is not byte-aligned (the codec works on whole
      // bytes).
      auto push = [&](uint16_t tick) -> bool {
        if (out.len >= out.capacity)
          return false;
        out.ticks[out.len++] = tick;
        return true;
      };
      for (size_t i = 0; i < detail::kPreambleBits; ++i)
        if (!push(esp32irpk::ac::detail::usToTicks(t.bit_mark_us)) ||
            !push(esp32irpk::ac::detail::usToTicks(t.zero_space_us)))
          return false;
      if (!push(esp32irpk::ac::detail::usToTicks(t.trailer_mark_us)) ||
          !push(esp32irpk::ac::detail::usToTicks(t.frame_gap_us)))
        return false;

      // Three sections, each with its own header (8 / 8 / 19 bytes).
      if (!bytesFrameToRaw(buf, detail::kSec1Len * 8, t, out))
        return false;
      if (!bytesFrameToRaw(buf + detail::kSec1Len, detail::kSec2Len * 8, t, out))
        return false;
      if (!bytesFrameToRaw(buf + detail::kSec1Len + detail::kSec2Len, detail::kSec3Len * 8, t, out))
        return false;
      return true;
    }
  };

} // namespace esp32irpk::ac::Daikin
