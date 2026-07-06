#pragma once

#include "../ESP32IRPulseKit.h"
#include "AcCodec.h"

// TCL air-conditioner support (the TCL112AC protocol: TCL TAC-09CHSD/XA31I and the
// many OEM-rebadged units — Leberg, Teknopoint, Daewoo, Electrolux GYKQ remotes).
// A full setting is a single 14-byte (112-bit) pulse-distance frame, LSB-first,
// beginning with the fixed 3-byte signature 23 CB 26, with a plain sum checksum in
// byte 13. See SPEC §11.2.
//
// The logical field map (byte/bit positions and the mode/fan/swing codes) is
// verified field-for-field against IRremoteESP8266's IRTcl112Ac via the
// compat_matrix_ac studies (tcl_irremoteesp8266_*).
//
// Targets the standard 14-byte TCL112AC "normal" message (no `Model` parameter).
// Temperature has 0.5 °C resolution. The Quiet, Light, Econo, Health, Turbo and the
// on/off timers are preserved across a decode->encode round-trip but are not
// settable here; the core Power/Mode/Temp/Fan/SwingV/SwingH controls are. The
// 96-bit TCL96AC and the "special/quiet" message type are separate frames.

namespace esp32irpk::ac::Tcl
{

  // Per-vendor enums; values are the wire codes. Mode lives in byte 6 bits 0-3.
  enum class Mode : uint8_t
  {
    HEAT = 1,
    DRY = 2,
    COOL = 3,
    FAN = 7,
    AUTO = 8,
  };

  // Fan lives in byte 8 bits 0-2. Wire codes are non-contiguous (no 4). MIN_SPEED
  // is the remote's "Night"/"Quiet" speed.
  enum class Fan : uint8_t
  {
    AUTO = 0,
    MIN_SPEED = 1,
    LOW_SPEED = 2,
    MED_SPEED = 3,
    HIGH_SPEED = 5,
  };

  // Vertical swing, byte 8 bits 3-5. Fixed positions plus a full-sweep "SWING". The
  // directional names avoid the Arduino HIGH/LOW macros ([[arduino-low-high-macro-enum]]).
  enum class SwingV : uint8_t
  {
    OFF = 0,
    UP = 1,          // highest
    MIDDLE_UP = 2,   // high
    MIDDLE = 3,
    MIDDLE_DOWN = 4, // low
    DOWN = 5,        // lowest
    SWING = 7,       // full sweep (on)
  };

  inline const char *toString(Mode m)
  {
    switch (m)
    {
    case Mode::HEAT: return "HEAT";
    case Mode::DRY: return "DRY";
    case Mode::COOL: return "COOL";
    case Mode::FAN: return "FAN";
    case Mode::AUTO: return "AUTO";
    }
    return "?";
  }
  inline const char *toString(Fan f)
  {
    switch (f)
    {
    case Fan::AUTO: return "AUTO";
    case Fan::MIN_SPEED: return "MIN_SPEED";
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
    case SwingV::MIDDLE_UP: return "MIDDLE_UP";
    case SwingV::MIDDLE: return "MIDDLE";
    case SwingV::MIDDLE_DOWN: return "MIDDLE_DOWN";
    case SwingV::DOWN: return "DOWN";
    case SwingV::SWING: return "SWING";
    }
    return "?";
  }

  namespace detail
  {
    // Standard TCL112AC timing (38kHz). LSB-first.
    inline constexpr AcTiming kTiming = {
        /*header_mark_us=*/3000,
        /*header_space_us=*/1650,
        /*bit_mark_us=*/500,
        /*zero_space_us=*/325,
        /*one_space_us=*/1050,
        /*trailer_mark_us=*/500,
        /*frame_gap_us=*/20000, // sent once; its real trailing gap is
                                // kDefaultMessageGap (100ms) but that silence is
                                // never captured, so any value larger than the
                                // longest data space that fits uint16_t is fine.
        /*tol_pct=*/40,         // wide decode window (see [[ac-decode-tolerance-loose]])
        /*lsb_first=*/true,
    };

    inline constexpr size_t kBytes = 14;
    inline constexpr uint8_t kSig0 = 0x23;
    inline constexpr uint8_t kSig1 = 0xCB;
    inline constexpr uint8_t kSig2 = 0x26;

    inline constexpr size_t kOffMsgType = 3;  // MsgType bits 0-1 (Normal=0b01)
    inline constexpr size_t kOffPower = 5;    // Power bit2, Quiet bit5, Light bit6, Econo bit7
    inline constexpr size_t kOffMode = 6;     // Mode bits 0-3, Health bit4, Turbo bit5
    inline constexpr size_t kOffTemp = 7;     // Temp bits 0-3
    inline constexpr size_t kOffFanSwing = 8; // Fan bits 0-2, SwingV bits 3-5
    inline constexpr size_t kOffSwingH = 12;  // SwingH bit3, HalfDegree bit5, isTcl bit7
    inline constexpr size_t kOffSum = 13;

    inline constexpr uint8_t kPowerMask = 0x04;
    inline constexpr uint8_t kModeMask = 0x0F;
    inline constexpr uint8_t kTempMask = 0x0F;
    inline constexpr uint8_t kFanMask = 0x07;
    inline constexpr uint8_t kSwingVMask = 0x38; // bits 3-5
    inline constexpr uint8_t kSwingHMask = 0x08; // bit 3
    inline constexpr uint8_t kHalfDegMask = 0x20; // bit 5 of byte 12

    inline constexpr uint8_t kTempMaxC = 31; // Temp field = 31 - floor(C)
    inline constexpr uint8_t kTempMinC = 16;

    // Checksum = plain sum of bytes 0..12 (Normal type). The "special" type
    // (byte 3 == 0x02) adds a 0xF offset; we only emit Normal but mirror the
    // reference so checksum_ok is accurate for a decoded special frame too.
    inline uint8_t calcChecksum(const uint8_t *b)
    {
      uint16_t sum = 0;
      for (size_t i = 0; i + 1 < kBytes; ++i)
        sum = static_cast<uint16_t>(sum + b[i]);
      if (b[kOffMsgType] == 0x02)
        sum = static_cast<uint16_t>(sum + 0xF);
      return static_cast<uint8_t>(sum & 0xFFu);
    }
    inline void writeChecksum(uint8_t *b) { b[kOffSum] = calcChecksum(b); }
    inline bool checksumOk(const uint8_t *b) { return b[kOffSum] == calcChecksum(b); }
    inline bool sigOk(const uint8_t *b)
    {
      return b[0] == kSig0 && b[1] == kSig1 && b[2] == kSig2;
    }

    inline Mode codeToMode(uint8_t c)
    {
      switch (c & kModeMask)
      {
      case 1: return Mode::HEAT;
      case 2: return Mode::DRY;
      case 3: return Mode::COOL;
      case 7: return Mode::FAN;
      default: return Mode::AUTO; // 8 and any invalid
      }
    }
    inline Fan codeToFan(uint8_t c)
    {
      switch (c & kFanMask)
      {
      case 1: return Fan::MIN_SPEED;
      case 2: return Fan::LOW_SPEED;
      case 3: return Fan::MED_SPEED;
      case 5: return Fan::HIGH_SPEED;
      default: return Fan::AUTO;
      }
    }
    inline SwingV codeToSwingV(uint8_t c)
    {
      switch (c & 0x07u)
      {
      case 1: return SwingV::UP;
      case 2: return SwingV::MIDDLE_UP;
      case 3: return SwingV::MIDDLE;
      case 4: return SwingV::MIDDLE_DOWN;
      case 5: return SwingV::DOWN;
      case 7: return SwingV::SWING;
      default: return SwingV::OFF;
      }
    }
  } // namespace detail

  struct Frame
  {
    static constexpr size_t kBytes = detail::kBytes; // 14
    static constexpr size_t kMaxTicks = 2 + 112 * 2 + 2 + 2; // header + bits + trailer + gap

    // raw state: a known-good frame (power on / cool / 24C / fan auto / no swing),
    // matching IRTcl112Ac::stateReset with the sum checksum recomputed.
    uint8_t bytes[kBytes] = {0x23, 0xCB, 0x26, 0x01, 0x00, 0x24, 0x03, 0x07,
                             0x40, 0x00, 0x00, 0x00, 0x00, 0x83};
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

    Mode mode() const { return detail::codeToMode(bytes[detail::kOffMode]); }
    void setMode(Mode m)
    {
      // Fan/Ventilation mode forces the fan to high (mirrors IRTcl112Ac::setMode).
      if (m == Mode::FAN)
        setFan(Fan::HIGH_SPEED);
      bytes[detail::kOffMode] = static_cast<uint8_t>(
          (bytes[detail::kOffMode] & ~detail::kModeMask) | (static_cast<uint8_t>(m) & detail::kModeMask));
    }

    Fan fan() const { return detail::codeToFan(bytes[detail::kOffFanSwing]); }
    void setFan(Fan f)
    {
      bytes[detail::kOffFanSwing] = static_cast<uint8_t>(
          (bytes[detail::kOffFanSwing] & ~detail::kFanMask) | (static_cast<uint8_t>(f) & detail::kFanMask));
    }

    // Temperature with 0.5 °C resolution. The wire Temp field is inverted
    // (31 − whole degrees) and the half-degree lives in byte 12 bit 5.
    float temperatureC() const
    {
      float t = static_cast<float>(detail::kTempMaxC) -
                static_cast<float>(bytes[detail::kOffTemp] & detail::kTempMask);
      if (bytes[detail::kOffSwingH] & detail::kHalfDegMask)
        t += 0.5f;
      return t;
    }
    void setTemperatureC(float celsius)
    {
      if (celsius < static_cast<float>(detail::kTempMinC)) celsius = detail::kTempMinC;
      if (celsius > static_cast<float>(detail::kTempMaxC)) celsius = detail::kTempMaxC;
      const uint8_t nr_half = static_cast<uint8_t>(celsius * 2.0f);
      if (nr_half & 1u)
        bytes[detail::kOffSwingH] |= detail::kHalfDegMask;
      else
        bytes[detail::kOffSwingH] &= static_cast<uint8_t>(~detail::kHalfDegMask);
      const uint8_t temp = static_cast<uint8_t>(detail::kTempMaxC - nr_half / 2);
      bytes[detail::kOffTemp] = static_cast<uint8_t>(
          (bytes[detail::kOffTemp] & ~detail::kTempMask) | (temp & detail::kTempMask));
    }

    SwingV swingV() const
    {
      return detail::codeToSwingV(static_cast<uint8_t>((bytes[detail::kOffFanSwing] & detail::kSwingVMask) >> 3));
    }
    void setSwingV(SwingV s)
    {
      bytes[detail::kOffFanSwing] = static_cast<uint8_t>(
          (bytes[detail::kOffFanSwing] & ~detail::kSwingVMask) | ((static_cast<uint8_t>(s) & 0x07u) << 3));
    }

    bool swingH() const { return (bytes[detail::kOffSwingH] & detail::kSwingHMask) != 0; }
    void setSwingH(bool on)
    {
      if (on)
        bytes[detail::kOffSwingH] |= detail::kSwingHMask;
      else
        bytes[detail::kOffSwingH] &= static_cast<uint8_t>(~detail::kSwingHMask);
    }

    void printTo(Print &out) const
    {
      printAcSummary(out, "TCL", power(), toString(mode()),
                     temperatureC(), toString(fan()), bytes,
                     byte_length, checksum_ok);
      out.print("// swingV=");
      out.print(toString(swingV()));
      out.print(" swingH=");
      out.println(swingH() ? "on" : "off");
    }

    void printSetterSnippet(Print &out) const
    {
      out.println("// send code (TCL AC, editable -- lossy: quiet/light/econo/health/turbo/timers use defaults):");
      out.println("esp32irpk::ac::Tcl::Frame f;");
      out.print("f.setPower(");
      out.print(power() ? "true" : "false");
      out.println(");");
      out.print("f.setMode(esp32irpk::ac::Tcl::Mode::");
      out.print(toString(mode()));
      out.println(");");
      out.print("f.setTemperatureC(");
      out.print(temperatureC(), 1);
      out.println("f);");
      out.print("f.setFan(esp32irpk::ac::Tcl::Fan::");
      out.print(toString(fan()));
      out.println(");");
      out.print("f.setSwingV(esp32irpk::ac::Tcl::SwingV::");
      out.print(toString(swingV()));
      out.println(");");
      out.print("f.setSwingH(");
      out.print(swingH() ? "true" : "false");
      out.println(");");
      out.println("esp32irpk::ac::send(tx, f);");
    }

    // RAW ticks -> state bytes. false if not a standard TCL112AC frame; checksum
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
      // Fixed 3-byte signature gate. Byte 2 == 0x26 is what the reference itself
      // uses to tell TCL112 apart from Mitsubishi112 (they share a decoder); the
      // full 23 CB 26 also disambiguates from the other ~3000-header vendors.
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

    // state -> RAW ticks. Rewrites the fixed signature, recomputes the sum
    // checksum, then renders the LSB-first 14-byte frame once
    // (kTcl112AcDefaultRepeat == kNoRepeat).
    bool toRaw(esp32irpk::IRRawTickBuffer &out) const
    {
      uint8_t buf[kBytes];
      for (size_t i = 0; i < kBytes; ++i)
        buf[i] = bytes[i];
      buf[0] = detail::kSig0;
      buf[1] = detail::kSig1;
      buf[2] = detail::kSig2;
      detail::writeChecksum(buf);

      out.len = 0;
      return bytesFrameToRaw(buf, kBytes * 8, detail::kTiming, out);
    }
  };

} // namespace esp32irpk::ac::Tcl
