#pragma once

#include "../ESP32IRPulseKit.h"
#include "AcCodec.h"

// Mitsubishi air-conditioner support (the 18-byte "Mitsubishi AC" protocol used
// by the common Kirigamine/MSZ remotes). One 18-byte pulse-distance frame with a
// fixed 5-byte signature, sent twice with a long gap between copies; the last
// byte is a sum checksum. See SPEC §11.2.
//
// Frame mechanics (timing, signature, checksum) follow the documented format.
// The logical field map (byte/bit positions and the mode/fan codes) is verified
// field-for-field against IRremoteESP8266's IRMitsubishiAC via the
// compat_matrix_ac studies (mitsubishi_irremoteesp8266_*).
//
// This wire format has a single model, so there is no `Model` parameter (SPEC
// §11.2, "two axes of variation"). The other Mitsubishi wire formats (136 / 112
// and Mitsubishi-Heavy) are different waveforms and would be separate Frame
// types, not models of this one.
//
// Transmit with the PHASE-ALIGNED carrier (setPhaseAlignedCarrier(true)): like
// Gree, the zero-space (420us) is shorter than the bit mark (450us), so the
// free-running hardware carrier's mark wobble can push spaces out of an external
// decoder's tolerance (SPEC §11.3).

namespace esp32irpk::ac::Mitsubishi
{

  // Per-vendor enums: only the values this protocol supports. Common members use
  // the shared naming convention (AUTO/COOL/HEAT/DRY/FAN, ...).
  enum class Mode : uint8_t
  {
    AUTO = 0,
    COOL,
    HEAT,
    DRY,
    FAN,
  };

  // Arduino defines LOW/HIGH as macros, so speed names carry the _SPEED suffix.
  // Mitsubishi exposes auto, a quiet step, and four fan speeds.
  enum class Fan : uint8_t
  {
    AUTO = 0,
    QUIET,
    LOW_SPEED,
    MED_SPEED,
    HIGH_SPEED,
    MAX_SPEED,
  };

  // Vertical airflow vane (byte 9 bits 3-5). AUTO lets the unit decide; SWING
  // sweeps. The fixed positions P1..P5 run highest (P1) to lowest (P5), matching
  // the P1..P5 convention used for Panasonic's louver (Arduino's HIGH/LOW macros
  // rule out directional names here).
  enum class Vane : uint8_t
  {
    AUTO = 0,
    P1 = 1, // highest
    P2 = 2,
    P3 = 3, // middle
    P4 = 4,
    P5 = 5, // lowest
    SWING = 7,
  };

  // Horizontal wide vane (byte 8 high nibble). Positions run left to right;
  // WIDE spreads the airflow and AUTO sweeps. NOTE: setMode rewrites byte 8 and
  // resets the wide vane to MIDDLE, so set the mode first, then the wide vane.
  enum class WideVane : uint8_t
  {
    LEFT_MAX = 1,
    LEFT = 2,
    MIDDLE = 3,
    RIGHT = 4,
    RIGHT_MAX = 5,
    WIDE = 6,
    AUTO = 8,
  };

  namespace detail
  {
    // Documented Mitsubishi AC pulse-distance timing.
    inline constexpr AcTiming kTiming = {
        /*header_mark_us=*/3400,
        /*header_space_us=*/1750,
        /*bit_mark_us=*/450,
        /*zero_space_us=*/420,
        /*one_space_us=*/1300,
        /*trailer_mark_us=*/440,
        /*frame_gap_us=*/15500,
        /*tol_pct=*/30,
        /*lsb_first=*/true,
    };

    inline constexpr size_t kBytes = 18;

    // Fixed 5-byte signature every Mitsubishi AC frame begins with.
    inline constexpr uint8_t kSignature[5] = {0x23, 0xCB, 0x26, 0x01, 0x00};

    // Byte offsets / masks for the logical fields.
    inline constexpr size_t kOffPower = 5;    // bit 5
    inline constexpr uint8_t kPowerMask = 0x20;
    inline constexpr size_t kOffMode = 6;     // bits 3-5
    inline constexpr size_t kOffTemp = 7;     // bits 0-3 (+ bit 4 = half degree)
    inline constexpr uint8_t kHalfDegreeBit = 0x10; // byte 7 bit 4 = +0.5C
    inline constexpr size_t kOffMode8 = 8;    // mode-specific byte (WideVane | mode)
    inline constexpr size_t kOffWideVane = 8; // same byte: high nibble = wide vane
    inline constexpr size_t kOffFan = 9;      // bits 0-2 + bit 7 (FanAuto)
    inline constexpr uint8_t kFanAutoMask = 0x80;
    inline constexpr size_t kOffVane = 9;     // same byte: bits 3-5 vane, bit 6 valid
    inline constexpr uint8_t kVaneMask = 0x38; // byte 9 bits 3-5
    inline constexpr uint8_t kVaneBit = 0x40;  // byte 9 bit 6 (vane value is valid)
    inline constexpr size_t kOffChecksum = 17;

    inline constexpr uint8_t kMinTempC = 16;
    inline constexpr uint8_t kMaxTempC = 31;

    // Sum checksum over bytes 0..16, stored in byte 17.
    inline uint8_t checksum(const uint8_t *b)
    {
      uint16_t sum = 0;
      for (size_t i = 0; i + 1 < kBytes; ++i)
        sum += b[i];
      return static_cast<uint8_t>(sum & 0xFF);
    }

    inline uint8_t modeToCode(Mode m)
    {
      switch (m)
      {
      case Mode::COOL: return 0b011;
      case Mode::DRY: return 0b010;
      case Mode::HEAT: return 0b001;
      case Mode::FAN: return 0b111;
      case Mode::AUTO: default: return 0b100;
      }
    }
    inline Mode codeToMode(uint8_t c)
    {
      switch (c)
      {
      case 0b011: return Mode::COOL;
      case 0b010: return Mode::DRY;
      case 0b001: return Mode::HEAT;
      case 0b111: return Mode::FAN;
      default: return Mode::AUTO;
      }
    }
    // Byte 8 carries a mode-specific value (high nibble = WideVane 0b0011, low
    // nibble = mode) that the protocol writes alongside the mode field.
    inline uint8_t modeToByte8(Mode m)
    {
      switch (m)
      {
      case Mode::COOL: return 0x36;
      case Mode::DRY: return 0x32;
      case Mode::FAN: return 0x37;
      case Mode::HEAT: return 0x30;
      case Mode::AUTO: default: return 0x30;
      }
    }
  } // namespace detail

  struct Frame
  {
    static constexpr size_t kBytes = detail::kBytes; // 18
    static constexpr size_t kMaxTicks = 640; // two rendered copies + headers/gaps

    // raw state: a known-good frame (signature + Power on / Heat / 22C / fan auto)
    // matching IRMitsubishiAC's reset state. Setters touch only the logical
    // fields; toRaw recomputes the checksum.
    uint8_t bytes[kBytes] = {
        0x23, 0xCB, 0x26, 0x01, 0x00, 0x20, 0x08, 0x06, 0x30,
        0x45, 0x67, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F};
    uint16_t byte_length = 0;
    bool checksum_ok = false;

    // Logical accessors over `bytes` (layout per the file header).
    bool power() const { return (bytes[detail::kOffPower] & detail::kPowerMask) != 0; }
    void setPower(bool on)
    {
      bytes[detail::kOffPower] = static_cast<uint8_t>(
          (bytes[detail::kOffPower] & ~detail::kPowerMask) | (on ? detail::kPowerMask : 0));
    }
    Mode mode() const { return detail::codeToMode((bytes[detail::kOffMode] >> 3) & 0x07u); }
    void setMode(Mode m)
    {
      bytes[detail::kOffMode] = static_cast<uint8_t>(
          (bytes[detail::kOffMode] & ~(0x07u << 3)) | (detail::modeToCode(m) << 3));
      bytes[detail::kOffMode8] = detail::modeToByte8(m);
    }
    // Temperature is a symmetric float pair carrying 0.5C steps: the integer
    // part is byte 7's low nibble (C - 16) and the +0.5 is byte 7 bit 4.
    float temperatureC() const
    {
      float c = static_cast<float>((bytes[detail::kOffTemp] & 0x0Fu) + detail::kMinTempC);
      if (bytes[detail::kOffTemp] & detail::kHalfDegreeBit) c += 0.5f;
      return c;
    }
    bool halfDegree() const
    {
      return (bytes[detail::kOffTemp] & detail::kHalfDegreeBit) != 0;
    }
    void setTemperatureC(float c)
    {
      if (c < detail::kMinTempC) c = static_cast<float>(detail::kMinTempC);
      if (c > detail::kMaxTempC) c = static_cast<float>(detail::kMaxTempC);
      // Round to the nearest 0.5C, then split into whole degrees + the half bit.
      uint8_t halfSteps = static_cast<uint8_t>(c * 2.0f + 0.5f);
      uint8_t whole = static_cast<uint8_t>(halfSteps / 2);
      bool half = (halfSteps & 1u) != 0;
      // Write bits 0-4 (temperature + half), keep bits 5-7.
      bytes[detail::kOffTemp] = static_cast<uint8_t>(
          (bytes[detail::kOffTemp] & 0xE0u) |
          ((whole - detail::kMinTempC) & 0x0Fu) |
          (half ? detail::kHalfDegreeBit : 0));
    }
    Fan fan() const
    {
      if (bytes[detail::kOffFan] & detail::kFanAutoMask)
        return Fan::AUTO;
      switch (bytes[detail::kOffFan] & 0x07u)
      {
      case 1: return Fan::LOW_SPEED;
      case 2: return Fan::MED_SPEED;
      case 3: return Fan::HIGH_SPEED;
      case 4: return Fan::MAX_SPEED;
      case 5: return Fan::QUIET;
      default: return Fan::AUTO;
      }
    }
    void setFan(Fan f)
    {
      uint8_t code = 0;       // byte9 bits 0-2
      bool fan_auto = false;  // byte9 bit 7
      switch (f)
      {
      case Fan::LOW_SPEED: code = 1; break;
      case Fan::MED_SPEED: code = 2; break;
      case Fan::HIGH_SPEED: code = 3; break;
      case Fan::MAX_SPEED: code = 4; break;
      case Fan::QUIET: code = 5; break;
      case Fan::AUTO: default: fan_auto = true; break;
      }
      bytes[detail::kOffFan] = static_cast<uint8_t>(
          (bytes[detail::kOffFan] & ~(0x07u | detail::kFanAutoMask)) | code |
          (fan_auto ? detail::kFanAutoMask : 0));
    }
    // Vertical vane (byte 9 bits 3-5). Setting any position also asserts the
    // "vane valid" bit (bit 6), as a real remote does.
    Vane vane() const
    {
      return static_cast<Vane>((bytes[detail::kOffVane] & detail::kVaneMask) >> 3);
    }
    void setVane(Vane v)
    {
      bytes[detail::kOffVane] = static_cast<uint8_t>(
          (bytes[detail::kOffVane] & ~(detail::kVaneMask | detail::kVaneBit)) |
          (static_cast<uint8_t>(v) << 3) | detail::kVaneBit);
    }
    // Horizontal wide vane (byte 8 high nibble). The low nibble is the mode
    // marker that setMode owns, so only the high nibble is touched here. Because
    // setMode rewrites the whole byte (resetting the wide vane to MIDDLE), call
    // setMode first and setWideVane after.
    WideVane wideVane() const
    {
      return static_cast<WideVane>((bytes[detail::kOffWideVane] >> 4) & 0x0Fu);
    }
    void setWideVane(WideVane v)
    {
      bytes[detail::kOffWideVane] = static_cast<uint8_t>(
          (bytes[detail::kOffWideVane] & 0x0Fu) | (static_cast<uint8_t>(v) << 4));
    }

    // Human-readable dump for diagnostics: the shared summary plus Mitsubishi's
    // vane fields and half-degree flag. Enum fields print as their raw code; the
    // hex line shows every byte.
    void printTo(Print &out) const
    {
      printAcSummary(out, "Mitsubishi", power(), static_cast<unsigned>(mode()),
                     temperatureC(), static_cast<unsigned>(fan()), bytes,
                     byte_length, checksum_ok);
      out.print("//   vane=");
      out.print(static_cast<unsigned>(vane()));
      out.print(" wideVane=");
      out.print(static_cast<unsigned>(wideVane()));
      out.print(" halfDegree=");
      out.println(halfDegree() ? "yes" : "no");
    }

    // RAW ticks -> state bytes. false if not a Mitsubishi AC frame; checksum
    // validity is reported separately via `checksum_ok`.
    static bool fromRaw(const esp32irpk::IRRawTickView &raw, Frame &out)
    {
      out = Frame{};
      size_t pos = 0;

      uint8_t b[detail::kBytes] = {};
      if (rawFrameToBytes(raw, pos, detail::kTiming, b, sizeof(b)) != detail::kBytes * 8)
        return false;
      // Vendor signature identifies Mitsubishi AC.
      if (b[0] != detail::kSignature[0] || b[1] != detail::kSignature[1] ||
          b[2] != detail::kSignature[2])
        return false;

      for (size_t i = 0; i < detail::kBytes; ++i)
        out.bytes[i] = b[i];
      out.byte_length = kBytes;
      out.checksum_ok = (b[detail::kOffChecksum] == detail::checksum(b));
      return true;
    }

    // state -> RAW ticks. Recomputes the checksum and renders the frame twice
    // (as a real remote does), separated by the inter-frame gap.
    bool toRaw(esp32irpk::IRRawTickBuffer &out) const
    {
      uint8_t buf[kBytes];
      for (size_t i = 0; i < kBytes; ++i)
        buf[i] = bytes[i];
      buf[detail::kOffChecksum] = detail::checksum(buf);

      out.len = 0;
      for (int copy = 0; copy < 2; ++copy)
        if (!bytesFrameToRaw(buf, kBytes * 8, detail::kTiming, out))
          return false;
      return true;
    }
  };

} // namespace esp32irpk::ac::Mitsubishi
