#pragma once

#include "../ESP32IRPulseKit.h"
#include "AcCodec.h"

// Panasonic air-conditioner support (Kaseikyo/AEHA family: two pulse-distance
// frames, LSB-first, frame 2 ends with a sum checksum). See SPEC §11.2.
//
// Frame mechanics (timing, two-frame layout, checksum) follow the documented
// Kaseikyo/Panasonic format. The logical field map (which byte/bit holds
// power/mode/temperature/fan, and the mode/fan codes) is verified byte-for-byte
// against IRremoteESP8266's IRPanasonicAc encoder
// (tests/studies/compat_matrix_ac/irremoteesp8266_tx).

namespace esp32irpk::ac::Panasonic
{

  // Per-vendor enums: only the values Panasonic supports. Common members use
  // the shared naming convention (AUTO/COOL/HEAT/DRY/FAN, ...).
  enum class Mode : uint8_t
  {
    AUTO = 0,
    COOL,
    HEAT,
    DRY,
    FAN,
  };

  // NOTE: Arduino defines `LOW`/`HIGH` as preprocessor macros (0/1), so bare
  // LOW/HIGH cannot be enumerators here. Use the `_SPEED` suffix (a single
  // token the `LOW`/`HIGH` macros do not match).
  enum class Fan : uint8_t
  {
    AUTO = 0,
    QUIET,
    LOW_SPEED,
    MED_SPEED,
    HIGH_SPEED,
    POWERFUL,
  };

  namespace detail
  {
    // Documented Kaseikyo/Panasonic pulse-distance timing.
    inline constexpr AcTiming kTiming = {
        /*header_mark_us=*/3456,
        /*header_space_us=*/1728,
        /*bit_mark_us=*/432,
        /*zero_space_us=*/432,
        /*one_space_us=*/1296,
        /*trailer_mark_us=*/432,
        /*frame_gap_us=*/10000,
        /*tol_pct=*/35,
        /*lsb_first=*/true,
    };

    inline constexpr size_t kFrame1Bytes = 8;
    inline constexpr size_t kFrame2Bytes = 19;

    // Fixed first frame and the shared vendor preamble of the second frame.
    inline constexpr uint8_t kFrame1[kFrame1Bytes] = {0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06};
    inline constexpr uint8_t kFrame2Preamble[5] = {0x02, 0x20, 0xE0, 0x04, 0x00};

    // Overall byte offsets (frame 2 begins at kFrame1Bytes).
    inline constexpr size_t kOffMode = kFrame1Bytes + 5;     // power bit + mode nibble
    inline constexpr size_t kOffTemp = kFrame1Bytes + 6;     // temperature
    inline constexpr size_t kOffFan = kFrame1Bytes + 8;      // fan nibble + swing nibble
    inline constexpr size_t kOffChecksum = kFrame1Bytes + kFrame2Bytes - 1;

    // Sum checksum over frame 2 excluding the checksum byte itself.
    inline uint8_t checksum(const uint8_t *frame2)
    {
      uint16_t sum = 0;
      for (size_t i = 0; i + 1 < kFrame2Bytes; ++i)
        sum += frame2[i];
      return static_cast<uint8_t>(sum & 0xFF);
    }

    // Mode/fan code maps. The fan nibble is the Panasonic speed plus 3
    // (min/low/med/high/max = 0x3..0x7, auto = 0xA).
    inline uint8_t modeToCode(Mode m)
    {
      switch (m)
      {
      case Mode::COOL: return 0x3;
      case Mode::HEAT: return 0x4;
      case Mode::DRY: return 0x2;
      case Mode::FAN: return 0x6;
      case Mode::AUTO: default: return 0x0;
      }
    }
    inline Mode codeToMode(uint8_t c)
    {
      switch (c)
      {
      case 0x3: return Mode::COOL;
      case 0x4: return Mode::HEAT;
      case 0x2: return Mode::DRY;
      case 0x6: return Mode::FAN;
      default: return Mode::AUTO;
      }
    }
    inline uint8_t fanToCode(Fan f)
    {
      switch (f)
      {
      case Fan::QUIET: return 0x3;
      case Fan::LOW_SPEED: return 0x4;
      case Fan::MED_SPEED: return 0x5;
      case Fan::HIGH_SPEED: return 0x6;
      case Fan::POWERFUL: return 0x7;
      case Fan::AUTO: default: return 0xA;
      }
    }
    inline Fan codeToFan(uint8_t c)
    {
      switch (c)
      {
      case 0x3: return Fan::QUIET;
      case 0x4: return Fan::LOW_SPEED;
      case 0x5: return Fan::MED_SPEED;
      case 0x6: return Fan::HIGH_SPEED;
      case 0x7: return Fan::POWERFUL;
      default: return Fan::AUTO;
      }
    }
  } // namespace detail

  struct Frame
  {
    static constexpr size_t kBytes = detail::kFrame1Bytes + detail::kFrame2Bytes; // 27
    static constexpr size_t kMaxTicks = 512; // two rendered frames + headers/gaps

    // raw state: frame1 (0..7) ++ frame2 (8..26). Default is a known-good frame
    // (signature, preamble, and the fixed feature bytes [15]=0x80, [19]=0x0E,
    // [20]=0xE0, [23]=0x81 that a real Panasonic frame always carries); the
    // logical fields (mode/power [13], temperature [14], fan [16]) and the
    // checksum [26] are zero until set, so a frame built from setters renders a
    // complete, decodable burst.
    uint8_t bytes[kBytes] = {
        0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x06,
        0x02, 0x20, 0xE0, 0x04, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
        0x00, 0x0E, 0xE0, 0x00, 0x00, 0x81, 0x00, 0x00, 0x00};
    uint16_t byte_length = 0;
    bool checksum_ok = false;

    // Logical accessors over `bytes` (layout per the file header).
    bool power() const { return (bytes[detail::kOffMode] & 0x01u) != 0; }
    void setPower(bool on)
    {
      bytes[detail::kOffMode] = static_cast<uint8_t>((bytes[detail::kOffMode] & ~0x01u) | (on ? 0x01u : 0x00u));
    }
    Mode mode() const { return detail::codeToMode((bytes[detail::kOffMode] >> 4) & 0x0Fu); }
    void setMode(Mode m)
    {
      bytes[detail::kOffMode] = static_cast<uint8_t>((bytes[detail::kOffMode] & 0x0Fu) | (detail::modeToCode(m) << 4));
    }
    uint8_t temperatureC() const { return static_cast<uint8_t>(bytes[detail::kOffTemp] >> 1); }
    void setTemperatureC(uint8_t c) { bytes[detail::kOffTemp] = static_cast<uint8_t>(c << 1); }
    Fan fan() const { return detail::codeToFan((bytes[detail::kOffFan] >> 4) & 0x0Fu); }
    void setFan(Fan f)
    {
      bytes[detail::kOffFan] = static_cast<uint8_t>((bytes[detail::kOffFan] & 0x0Fu) | (detail::fanToCode(f) << 4));
    }

    // RAW ticks -> state bytes. false if not a Panasonic two-frame burst;
    // checksum validity is reported separately via `checksum_ok`.
    static bool fromRaw(const esp32irpk::IRRawTickView &raw, Frame &out)
    {
      out = Frame{};
      size_t pos = 0;

      uint8_t f1[detail::kFrame1Bytes] = {};
      if (rawFrameToBytes(raw, pos, detail::kTiming, f1, sizeof(f1)) != detail::kFrame1Bytes * 8)
        return false;
      // Vendor signature identifies Panasonic/Kaseikyo.
      if (f1[0] != detail::kFrame1[0] || f1[1] != detail::kFrame1[1])
        return false;

      uint8_t f2[detail::kFrame2Bytes] = {};
      if (rawFrameToBytes(raw, pos, detail::kTiming, f2, sizeof(f2)) != detail::kFrame2Bytes * 8)
        return false;

      for (size_t i = 0; i < detail::kFrame1Bytes; ++i)
        out.bytes[i] = f1[i];
      for (size_t i = 0; i < detail::kFrame2Bytes; ++i)
        out.bytes[detail::kFrame1Bytes + i] = f2[i];
      out.byte_length = kBytes;
      out.checksum_ok = (f2[detail::kFrame2Bytes - 1] == detail::checksum(f2));
      return true;
    }

    // state -> RAW ticks. Stamps the fixed signature/preamble and recomputes the
    // checksum, so a frame built from setters renders a valid burst.
    bool toRaw(esp32irpk::IRRawTickBuffer &out) const
    {
      uint8_t buf[kBytes];
      for (size_t i = 0; i < kBytes; ++i)
        buf[i] = bytes[i];
      for (size_t i = 0; i < detail::kFrame1Bytes; ++i)
        buf[i] = detail::kFrame1[i];
      for (size_t i = 0; i < sizeof(detail::kFrame2Preamble); ++i)
        buf[detail::kFrame1Bytes + i] = detail::kFrame2Preamble[i];
      buf[detail::kOffChecksum] = detail::checksum(buf + detail::kFrame1Bytes);

      out.len = 0;
      if (!bytesFrameToRaw(buf, detail::kFrame1Bytes * 8, detail::kTiming, out))
        return false;
      if (!bytesFrameToRaw(buf + detail::kFrame1Bytes, detail::kFrame2Bytes * 8, detail::kTiming, out))
        return false;
      return true;
    }
  };

} // namespace esp32irpk::ac::Panasonic
