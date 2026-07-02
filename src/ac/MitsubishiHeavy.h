#pragma once

#include "../ESP32IRPulseKit.h"
#include "AcCodec.h"

// Mitsubishi Heavy Industries air-conditioner support (the MITSUBISHI_HEAVY_152
// protocol: SRKxxZM-S / SRKxxZMXA-S "Beaver" split units, RLA502A700B remote).
// A full setting is a single 19-byte (152-bit) pulse-distance frame, LSB-first,
// beginning with the fixed 5-byte signature AD 51 3C E5 1A. See SPEC §11.2.
//
// This is a different manufacturer from the "Mitsubishi" (Mitsubishi Electric
// MSZ) 18-byte vendor -- Mitsubishi Heavy Industries is its own vendor namespace.
//
// The format has no arithmetic checksum: instead every field-bearing byte from
// offset 3 on is followed by its bitwise complement (inverted byte pairs). Note
// the wire uses a SHORTER space for a 1-bit (420us) than a 0-bit (1220us), the
// reverse of the usual pulse-distance convention; the shared AcCodec classifies
// spaces by nearest nominal length, so this works transparently.
//
// The logical field map (byte/bit positions and the mode/fan/swing codes) is
// verified field-for-field against IRremoteESP8266's IRMitsubishiHeavy152Ac via
// the compat_matrix_ac studies (mitsubishiheavy_irremoteesp8266_*).
//
// Targets the 152-bit format (no `Model` parameter). The 88-bit SRKxxZJ-S format
// is a separate frame type. The Night, Silent, Filter, Clean and 3D flags are
// preserved across a decode->encode round-trip but are not settable here; the
// core Power/Mode/Temp/Fan/SwingV/SwingH controls are.

namespace esp32irpk::ac::MitsubishiHeavy
{

  // Per-vendor enums; values are the wire codes.
  // Mode lives in byte 5 bits 0-2.
  enum class Mode : uint8_t
  {
    AUTO = 0,
    COOL = 1,
    DRY = 2,
    FAN = 3,
    HEAT = 4,
  };

  // Fan lives in byte 9 bits 0-3. Enum values ARE the wire codes; Econo (0x6) and
  // Turbo (0x8) are the two special speeds that share this field.
  enum class Fan : uint8_t
  {
    AUTO = 0,
    LOW_SPEED = 1,
    MED_SPEED = 2,
    HIGH_SPEED = 3,
    MAX_SPEED = 4,
    ECONO = 6,
    TURBO = 8,
  };

  // Vertical swing, byte 11 bits 5-7. The five fixed positions map top-to-bottom;
  // named after Gree's directional vocabulary (HIGH/LOW are Arduino macros and
  // cannot be enum members, see [[arduino-low-high-macro-enum]]). Wire order:
  // 0=auto, 1=highest, 2=high, 3=middle, 4=low, 5=lowest, 6=off.
  enum class SwingV : uint8_t
  {
    AUTO = 0,
    UP = 1,          // highest
    MIDDLE_UP = 2,   // high
    MIDDLE = 3,
    MIDDLE_DOWN = 4, // low
    DOWN = 5,        // lowest
    OFF = 6,
  };

  // Horizontal swing, byte 13 bits 0-3.
  enum class SwingH : uint8_t
  {
    AUTO = 0,
    LEFT_MAX = 1,
    LEFT = 2,
    MIDDLE = 3,
    RIGHT = 4,
    RIGHT_MAX = 5,
    RIGHT_LEFT = 6,
    LEFT_RIGHT = 7,
    OFF = 8,
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
    case Fan::ECONO: return "ECONO";
    case Fan::TURBO: return "TURBO";
    }
    return "?";
  }
  inline const char *toString(SwingV s)
  {
    switch (s)
    {
    case SwingV::AUTO: return "AUTO";
    case SwingV::UP: return "UP";
    case SwingV::MIDDLE_UP: return "MIDDLE_UP";
    case SwingV::MIDDLE: return "MIDDLE";
    case SwingV::MIDDLE_DOWN: return "MIDDLE_DOWN";
    case SwingV::DOWN: return "DOWN";
    case SwingV::OFF: return "OFF";
    }
    return "?";
  }
  inline const char *toString(SwingH s)
  {
    switch (s)
    {
    case SwingH::AUTO: return "AUTO";
    case SwingH::LEFT_MAX: return "LEFT_MAX";
    case SwingH::LEFT: return "LEFT";
    case SwingH::MIDDLE: return "MIDDLE";
    case SwingH::RIGHT: return "RIGHT";
    case SwingH::RIGHT_MAX: return "RIGHT_MAX";
    case SwingH::RIGHT_LEFT: return "RIGHT_LEFT";
    case SwingH::LEFT_RIGHT: return "LEFT_RIGHT";
    case SwingH::OFF: return "OFF";
    }
    return "?";
  }

  namespace detail
  {
    // MITSUBISHI_HEAVY_152 timing (38kHz). LSB-first. Note one_space (1-bit) is
    // SHORTER than zero_space (0-bit) -- AcCodec classifies by nearest length.
    inline constexpr AcTiming kTiming = {
        /*header_mark_us=*/3140,
        /*header_space_us=*/1630,
        /*bit_mark_us=*/370,
        /*zero_space_us=*/1220,
        /*one_space_us=*/420,
        /*trailer_mark_us=*/370,
        /*frame_gap_us=*/20000, // sent once; its real trailing gap is
                                // kDefaultMessageGap (100ms) but that silence is
                                // never captured, so any value larger than the
                                // longest data space that fits uint16_t is fine.
        /*tol_pct=*/40,         // wide decode window (see [[ac-decode-tolerance-loose]])
        /*lsb_first=*/true,
    };

    inline constexpr size_t kBytes = 19;
    inline constexpr size_t kSigLen = 5;
    inline constexpr uint8_t kSig[kSigLen] = {0xAD, 0x51, 0x3C, 0xE5, 0x1A};

    // Field-bearing bytes (odd offsets from 5 on); the following even byte holds
    // the complement. Byte 3/4 are part of the signature but also a valid pair.
    inline constexpr size_t kOffModePower = 5; // Mode 0-2, Power 3, Clean 5, Filter 6
    inline constexpr size_t kOffTemp = 7;      // Temp 0-3 (degC - 17)
    inline constexpr size_t kOffFan = 9;       // Fan 0-3
    inline constexpr size_t kOffSwing3D = 11;  // Three 1, D 4, SwingV 5-7
    inline constexpr size_t kOffSwingH = 13;   // SwingH 0-3
    inline constexpr size_t kOffNightSilent = 15; // Night 6, Silent 7
    inline constexpr size_t kByte17 = 17;      // fixed 0x80

    inline constexpr uint8_t kModeMask = 0x07;
    inline constexpr uint8_t kPowerMask = 0x08;
    inline constexpr uint8_t kCleanMask = 0x20;
    inline constexpr uint8_t kFilterMask = 0x40;
    inline constexpr uint8_t kTempMask = 0x0F;
    inline constexpr uint8_t kFanMask = 0x0F;
    inline constexpr uint8_t kSwingVMask = 0xE0; // bits 5-7
    inline constexpr uint8_t kThreeMask = 0x02;  // bit 1
    inline constexpr uint8_t kDMask = 0x10;      // bit 4
    inline constexpr uint8_t k3DMask = 0x12;     // Three | D
    inline constexpr uint8_t kSwingHMask = 0x0F;
    inline constexpr uint8_t kNightMask = 0x40;
    inline constexpr uint8_t kSilentMask = 0x80;

    inline constexpr uint8_t kMinTempC = 17;
    inline constexpr uint8_t kMaxTempC = 31;

    // "Checksum": no arithmetic sum -- byte pairs from offset 3 are inverted, i.e.
    // byte[i+1] == ~byte[i] for i = 3,5,7,...,17. (Matches IRremoteESP8266's
    // invertBytePairs / checkInvertedBytePairs.)
    inline void writeChecksum(uint8_t *b)
    {
      for (size_t i = 3; i + 1 < kBytes; i += 2)
        b[i + 1] = static_cast<uint8_t>(~b[i]);
    }
    inline bool checksumOk(const uint8_t *b)
    {
      for (size_t i = 3; i + 1 < kBytes; i += 2)
        if (b[i + 1] != static_cast<uint8_t>(~b[i]))
          return false;
      return true;
    }
    inline bool sigOk(const uint8_t *b)
    {
      for (size_t i = 0; i < kSigLen; ++i)
        if (b[i] != kSig[i])
          return false;
      return true;
    }

    inline Mode codeToMode(uint8_t c)
    {
      switch (c & kModeMask)
      {
      case 1: return Mode::COOL;
      case 2: return Mode::DRY;
      case 3: return Mode::FAN;
      case 4: return Mode::HEAT;
      default: return Mode::AUTO; // 0 and any invalid
      }
    }
    inline Fan codeToFan(uint8_t c)
    {
      switch (c & kFanMask)
      {
      case 1: return Fan::LOW_SPEED;
      case 2: return Fan::MED_SPEED;
      case 3: return Fan::HIGH_SPEED;
      case 4: return Fan::MAX_SPEED;
      case 6: return Fan::ECONO;
      case 8: return Fan::TURBO;
      default: return Fan::AUTO;
      }
    }
    inline SwingV codeToSwingV(uint8_t c)
    {
      if (c > static_cast<uint8_t>(SwingV::OFF))
        c = static_cast<uint8_t>(SwingV::OFF);
      return static_cast<SwingV>(c);
    }
    inline SwingH codeToSwingH(uint8_t c)
    {
      if (c > static_cast<uint8_t>(SwingH::OFF))
        c = static_cast<uint8_t>(SwingH::OFF);
      return static_cast<SwingH>(c);
    }
  } // namespace detail

  struct Frame
  {
    static constexpr size_t kBytes = detail::kBytes; // 19
    static constexpr size_t kMaxTicks = 2 + 152 * 2 + 2 + 2; // header + bits + trailer + gap

    // raw state: a known-good frame (power on / cool / 24C / fan auto / swingV off
    // / swingH off), inverted byte pairs precomputed.
    uint8_t bytes[kBytes] = {0xAD, 0x51, 0x3C, 0xE5, 0x1A, 0x09, 0xF6, 0x07, 0xF8,
                             0x00, 0xFF, 0xC0, 0x3F, 0x08, 0xF7, 0x00, 0xFF, 0x80, 0x7F};
    uint16_t byte_length = 0;
    bool checksum_ok = false;

    bool power() const { return (bytes[detail::kOffModePower] & detail::kPowerMask) != 0; }
    void setPower(bool on)
    {
      if (on)
        bytes[detail::kOffModePower] |= detail::kPowerMask;
      else
        bytes[detail::kOffModePower] &= static_cast<uint8_t>(~detail::kPowerMask);
    }

    Mode mode() const { return detail::codeToMode(bytes[detail::kOffModePower]); }
    void setMode(Mode m)
    {
      bytes[detail::kOffModePower] = static_cast<uint8_t>(
          (bytes[detail::kOffModePower] & ~detail::kModeMask) | (static_cast<uint8_t>(m) & detail::kModeMask));
    }

    Fan fan() const { return detail::codeToFan(bytes[detail::kOffFan]); }
    void setFan(Fan f)
    {
      bytes[detail::kOffFan] = static_cast<uint8_t>(
          (bytes[detail::kOffFan] & ~detail::kFanMask) | (static_cast<uint8_t>(f) & detail::kFanMask));
    }

    // Whole-degree temperature, byte 7 low nibble = °C - 17, clamped 17..31.
    uint8_t temperatureC() const
    {
      return static_cast<uint8_t>((bytes[detail::kOffTemp] & detail::kTempMask) + detail::kMinTempC);
    }
    void setTemperatureC(uint8_t c)
    {
      if (c < detail::kMinTempC) c = detail::kMinTempC;
      if (c > detail::kMaxTempC) c = detail::kMaxTempC;
      bytes[detail::kOffTemp] = static_cast<uint8_t>(
          (bytes[detail::kOffTemp] & ~detail::kTempMask) | ((c - detail::kMinTempC) & detail::kTempMask));
    }

    SwingV swingV() const
    {
      return detail::codeToSwingV(static_cast<uint8_t>((bytes[detail::kOffSwing3D] & detail::kSwingVMask) >> 5));
    }
    void setSwingV(SwingV s)
    {
      bytes[detail::kOffSwing3D] = static_cast<uint8_t>(
          (bytes[detail::kOffSwing3D] & ~detail::kSwingVMask) | ((static_cast<uint8_t>(s) & 0x07u) << 5));
    }

    SwingH swingH() const
    {
      return detail::codeToSwingH(static_cast<uint8_t>(bytes[detail::kOffSwingH] & detail::kSwingHMask));
    }
    void setSwingH(SwingH s)
    {
      bytes[detail::kOffSwingH] = static_cast<uint8_t>(
          (bytes[detail::kOffSwingH] & ~detail::kSwingHMask) | (static_cast<uint8_t>(s) & detail::kSwingHMask));
    }

    void printTo(Print &out) const
    {
      printAcSummary(out, "MitsubishiHeavy", power(), toString(mode()),
                     static_cast<float>(temperatureC()), toString(fan()), bytes,
                     byte_length, checksum_ok);
      out.print("// swingV=");
      out.print(toString(swingV()));
      out.print(" swingH=");
      out.println(toString(swingH()));
    }

    void printSetterSnippet(Print &out) const
    {
      out.println("// send code (Mitsubishi Heavy AC, editable -- lossy: night/silent/clean/filter/3D use defaults):");
      out.println("esp32irpk::ac::MitsubishiHeavy::Frame f;");
      out.print("f.setPower(");
      out.print(power() ? "true" : "false");
      out.println(");");
      out.print("f.setMode(esp32irpk::ac::MitsubishiHeavy::Mode::");
      out.print(toString(mode()));
      out.println(");");
      out.print("f.setTemperatureC(");
      out.print((unsigned)temperatureC());
      out.println(");");
      out.print("f.setFan(esp32irpk::ac::MitsubishiHeavy::Fan::");
      out.print(toString(fan()));
      out.println(");");
      out.print("f.setSwingV(esp32irpk::ac::MitsubishiHeavy::SwingV::");
      out.print(toString(swingV()));
      out.println(");");
      out.print("f.setSwingH(esp32irpk::ac::MitsubishiHeavy::SwingH::");
      out.print(toString(swingH()));
      out.println(");");
      out.println("esp32irpk::ac::send(tx, f);");
    }

    // RAW ticks -> state bytes. false if not a standard MITSUBISHI_HEAVY_152 frame;
    // checksum (inverted-pair) validity is reported separately via `checksum_ok`.
    static bool fromRaw(const esp32irpk::IRRawTickView &raw, Frame &out)
    {
      out = Frame{};
      if (!raw.ticks)
        return false;
      size_t pos = 0;
      uint8_t b[detail::kBytes] = {};
      if (rawFrameToBytes(raw, pos, detail::kTiming, b, detail::kBytes) != detail::kBytes * 8)
        return false;
      // Fixed 5-byte signature gate (disambiguates from the ~3400/1700 header
      // group: Mitsubishi Electric / Panasonic / Fujitsu / Daikin / Hitachi).
      if (!detail::sigOk(b))
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

    // state -> RAW ticks. Rewrites the fixed signature, recomputes the inverted
    // byte pairs, then renders the LSB-first 19-byte frame once
    // (kMitsubishiHeavy152MinRepeat == kNoRepeat).
    bool toRaw(esp32irpk::IRRawTickBuffer &out) const
    {
      uint8_t buf[kBytes];
      for (size_t i = 0; i < kBytes; ++i)
        buf[i] = bytes[i];
      for (size_t i = 0; i < detail::kSigLen; ++i)
        buf[i] = detail::kSig[i];
      buf[detail::kByte17] = 0x80;
      detail::writeChecksum(buf);

      out.len = 0;
      return bytesFrameToRaw(buf, kBytes * 8, detail::kTiming, out);
    }
  };

} // namespace esp32irpk::ac::MitsubishiHeavy
