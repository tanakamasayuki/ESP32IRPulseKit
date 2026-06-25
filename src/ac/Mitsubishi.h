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
    inline constexpr size_t kOffMode8 = 8;    // mode-specific byte (WideVane | mode)
    inline constexpr size_t kOffFan = 9;      // bits 0-2 + bit 7 (FanAuto)
    inline constexpr uint8_t kFanAutoMask = 0x80;
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
    uint8_t temperatureC() const
    {
      return static_cast<uint8_t>((bytes[detail::kOffTemp] & 0x0Fu) + detail::kMinTempC);
    }
    void setTemperatureC(uint8_t c)
    {
      if (c < detail::kMinTempC) c = detail::kMinTempC;
      if (c > detail::kMaxTempC) c = detail::kMaxTempC;
      // Set the 4-bit temperature, clear the half-degree bit (bit 4), keep the rest.
      bytes[detail::kOffTemp] = static_cast<uint8_t>(
          (bytes[detail::kOffTemp] & 0xE0u) | ((c - detail::kMinTempC) & 0x0Fu));
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
