#pragma once

#include "../ESP32IRPulseKit.h"
#include "AcCodec.h"

// Gree air-conditioner support (also used by many OEM rebrands). One 8-byte
// state sent as two pulse-distance blocks: block 1 carries bytes 0..3 followed
// by a fixed 3-bit footer (0b010) and a long gap; block 2 carries bytes 4..7
// with NO header of its own. A Kelvinator-style block checksum occupies the
// high nibble of byte 7. See SPEC §11.2.
//
// Frame mechanics (timing, two-block layout, checksum) follow the documented
// Gree format. The state targets the YBOFB-style single model: the model bit
// (byte 2, bit 6) stays clear, so byte 2 is a stable 0x20 and does not change
// with power. The logical field map (byte/bit positions and the mode/fan codes)
// is verified field-for-field against IRremoteESP8266's IRGreeAC (YBOFB model)
// via the compat_matrix_ac studies (gree_irremoteesp8266_*).
//
// Transmit Gree with the PHASE-ALIGNED carrier (setPhaseAlignedCarrier(true)).
// Gree's zero-space (540us) is shorter than its bit mark (620us), so the
// free-running hardware carrier's ~1-cycle (~26us) mark wobble drops about half
// the frames over the air (measured PA 50/50 vs hardware ~55%, see SPEC 11.3).

namespace esp32irpk::ac::Gree
{

  // Per-vendor enums: only the values Gree supports. Common members use the
  // shared naming convention (AUTO/COOL/HEAT/DRY/FAN, ...).
  enum class Mode : uint8_t
  {
    AUTO = 0,
    COOL,
    HEAT,
    DRY,
    FAN,
  };

  // NOTE: Arduino defines `LOW`/`HIGH` as preprocessor macros, so speed names
  // carry the `_SPEED` suffix (a single token the macros do not match), matching
  // the other vendors. Gree exposes three fixed speeds plus auto.
  enum class Fan : uint8_t
  {
    AUTO = 0,
    MIN_SPEED,
    MED_SPEED,
    MAX_SPEED,
  };

  namespace detail
  {
    // Documented Gree pulse-distance timing. Block 2 carries no header; the
    // shared codec is told so via `with_header=false`.
    inline constexpr AcTiming kTiming = {
        /*header_mark_us=*/9000,
        /*header_space_us=*/4500,
        /*bit_mark_us=*/620,
        /*zero_space_us=*/540,
        /*one_space_us=*/1600,
        /*trailer_mark_us=*/620,
        /*frame_gap_us=*/19980,
        /*tol_pct=*/30,
        /*lsb_first=*/true,
    };

    inline constexpr size_t kBytes = 8;
    inline constexpr size_t kBlockBytes = 4; // each block carries 4 data bytes

    // Block 1's 32 data bits are followed by a constant 3-bit footer (0b010,
    // LSB-first => bits 0,1,0). Decoding reads 35 bits into a 5-byte buffer; the
    // footer lands in the low 3 bits of the 5th byte.
    inline constexpr uint8_t kBlock1FooterByte = 0x02; // bit0=0,bit1=1,bit2=0
    inline constexpr size_t kBlock1Bits = kBlockBytes * 8 + 3; // 35

    inline constexpr uint8_t kChecksumStart = 10;

    // Temperature range (Celsius) stored as (C - 16) in byte 1's low nibble.
    inline constexpr uint8_t kMinTempC = 16;
    inline constexpr uint8_t kMaxTempC = 30;

    // Kelvinator-style block checksum: start + lower nibbles of bytes 0..3 +
    // upper nibbles of bytes 4..6, mod 16. Stored in byte 7's high nibble.
    inline uint8_t checksum(const uint8_t *b)
    {
      uint8_t sum = kChecksumStart;
      for (size_t i = 0; i < 4; ++i)
        sum += (b[i] & 0x0Fu);
      for (size_t i = 4; i < 7; ++i)
        sum += (b[i] >> 4);
      return sum & 0x0Fu;
    }

    inline uint8_t modeToCode(Mode m)
    {
      switch (m)
      {
      case Mode::COOL: return 0x1;
      case Mode::DRY: return 0x2;
      case Mode::FAN: return 0x3;
      case Mode::HEAT: return 0x4;
      case Mode::AUTO: default: return 0x0;
      }
    }
    inline Mode codeToMode(uint8_t c)
    {
      switch (c)
      {
      case 0x1: return Mode::COOL;
      case 0x2: return Mode::DRY;
      case 0x3: return Mode::FAN;
      case 0x4: return Mode::HEAT;
      default: return Mode::AUTO;
      }
    }
    inline uint8_t fanToCode(Fan f)
    {
      switch (f)
      {
      case Fan::MIN_SPEED: return 0x1;
      case Fan::MED_SPEED: return 0x2;
      case Fan::MAX_SPEED: return 0x3;
      case Fan::AUTO: default: return 0x0;
      }
    }
    inline Fan codeToFan(uint8_t c)
    {
      switch (c)
      {
      case 0x1: return Fan::MIN_SPEED;
      case 0x2: return Fan::MED_SPEED;
      case 0x3: return Fan::MAX_SPEED;
      default: return Fan::AUTO;
      }
    }
  } // namespace detail

  struct Frame
  {
    static constexpr size_t kBytes = detail::kBytes; // 8
    static constexpr size_t kMaxTicks = 256; // two blocks + headers/gaps

    // raw state: a known-good frame (Mode Auto, Power off, Fan Auto, 25C). The
    // fixed bits Gree always carries (byte3 high nibble 0b0101, byte5 marker)
    // live in this template; setters touch only the logical fields, and toRaw
    // recomputes the checksum.
    uint8_t bytes[kBytes] = {0x00, 0x09, 0x20, 0x50, 0x00, 0x20, 0x00, 0x50};
    uint16_t byte_length = 0;
    bool checksum_ok = false;

    // Logical accessors over `bytes` (layout per the file header).
    bool power() const { return (bytes[0] & 0x08u) != 0; }
    void setPower(bool on)
    {
      bytes[0] = static_cast<uint8_t>((bytes[0] & ~0x08u) | (on ? 0x08u : 0x00u));
    }
    Mode mode() const { return detail::codeToMode(bytes[0] & 0x07u); }
    void setMode(Mode m)
    {
      bytes[0] = static_cast<uint8_t>((bytes[0] & ~0x07u) | detail::modeToCode(m));
    }
    uint8_t temperatureC() const
    {
      return static_cast<uint8_t>((bytes[1] & 0x0Fu) + detail::kMinTempC);
    }
    void setTemperatureC(uint8_t c)
    {
      if (c < detail::kMinTempC) c = detail::kMinTempC;
      if (c > detail::kMaxTempC) c = detail::kMaxTempC;
      bytes[1] = static_cast<uint8_t>((bytes[1] & 0xF0u) | ((c - detail::kMinTempC) & 0x0Fu));
    }
    Fan fan() const { return detail::codeToFan((bytes[0] >> 4) & 0x03u); }
    void setFan(Fan f)
    {
      bytes[0] = static_cast<uint8_t>((bytes[0] & ~0x30u) | (detail::fanToCode(f) << 4));
    }

    // RAW ticks -> state bytes. false if not a Gree two-block burst; checksum
    // validity is reported separately via `checksum_ok`.
    static bool fromRaw(const esp32irpk::IRRawTickView &raw, Frame &out)
    {
      out = Frame{};
      size_t pos = 0;

      // Block 1: header + 32 data bits + 3-bit footer (35 bits into 5 bytes).
      uint8_t b1[detail::kBlockBytes + 1] = {};
      if (rawFrameToBytes(raw, pos, detail::kTiming, b1, sizeof(b1), true) !=
          detail::kBlock1Bits)
        return false;
      // Verify the constant footer (low 3 bits of the 5th byte).
      if ((b1[detail::kBlockBytes] & 0x07u) != detail::kBlock1FooterByte)
        return false;

      // Block 2: no header, 32 data bits.
      uint8_t b2[detail::kBlockBytes] = {};
      if (rawFrameToBytes(raw, pos, detail::kTiming, b2, sizeof(b2), false) !=
          detail::kBlockBytes * 8)
        return false;

      for (size_t i = 0; i < detail::kBlockBytes; ++i)
        out.bytes[i] = b1[i];
      for (size_t i = 0; i < detail::kBlockBytes; ++i)
        out.bytes[detail::kBlockBytes + i] = b2[i];
      out.byte_length = kBytes;
      out.checksum_ok = (((out.bytes[7] >> 4) & 0x0Fu) == detail::checksum(out.bytes));
      return true;
    }

    // state -> RAW ticks. Recomputes the checksum, so a frame built from setters
    // renders a valid two-block burst.
    bool toRaw(esp32irpk::IRRawTickBuffer &out) const
    {
      uint8_t buf[kBytes];
      for (size_t i = 0; i < kBytes; ++i)
        buf[i] = bytes[i];
      buf[7] = static_cast<uint8_t>((buf[7] & 0x0Fu) | (detail::checksum(buf) << 4));

      out.len = 0;
      // Block 1: bytes 0..3 plus the constant 3-bit footer (35 bits, with header).
      uint8_t block1[detail::kBlockBytes + 1] = {
          buf[0], buf[1], buf[2], buf[3], detail::kBlock1FooterByte};
      if (!bytesFrameToRaw(block1, detail::kBlock1Bits, detail::kTiming, out, true))
        return false;
      // Block 2: bytes 4..7 (32 bits, no header).
      if (!bytesFrameToRaw(buf + detail::kBlockBytes, detail::kBlockBytes * 8,
                           detail::kTiming, out, false))
        return false;
      return true;
    }
  };

} // namespace esp32irpk::ac::Gree
