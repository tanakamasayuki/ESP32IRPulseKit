#pragma once

#include "../ESP32IRPulseKit.h"
#include "AcCodec.h"

// Toshiba air-conditioner support (the standard 9-byte TOSHIBA_AC protocol used by
// the WH-/RAS- series remotes and rebadged Carrier units). A full setting is a
// single 9-byte pulse-distance frame, MSB-first, beginning with the fixed
// signature 0xF2 0x0D, with an XOR checksum in the last byte. Bytes come in
// inverted pairs (byte1 = ~byte0, byte3 = ~byte2). See SPEC §11.2.
//
// Frame mechanics (timing, signature, XOR checksum) follow the documented format.
// The logical field map (byte/bit positions and the mode/fan codes) is verified
// field-for-field against IRremoteESP8266's IRToshibaAC via the compat_matrix_ac
// studies (toshiba_irremoteesp8266_*).
//
// This targets the standard 9-byte format (no `Model` parameter). The 7-byte
// "short" swing message and the 10-byte "long" message are different frames and
// would be separate Frame types (SPEC §11.2); swing is therefore not a settable
// field here (the standard frame does not carry it).
//
// Power is carried by the Mode field, not a separate bit: Mode == 7 means off, so
// `setPower(false)` writes Mode 7 and `setPower(true)` restores the last set mode.

namespace esp32irpk::ac::Toshiba
{

  // Per-vendor enums; values are the wire codes. Mode lives in byte 6 bits 0-2.
  enum class Mode : uint8_t
  {
    AUTO = 0,
    COOL = 1,
    DRY = 2,
    HEAT = 3,
    FAN = 4,
  };

  // Fan lives in byte 6 bits 5-7. The wire codes are non-contiguous: auto=0, then
  // 2..6 for the five speeds (code 1 is unused). Enum values ARE the wire codes.
  enum class Fan : uint8_t
  {
    AUTO = 0,
    MIN_SPEED = 2,
    LOW_SPEED = 3,
    MED_SPEED = 4,
    HIGH_SPEED = 5,
    MAX_SPEED = 6,
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
    // Standard TOSHIBA_AC timing. MSB-first (lsb_first = false). The documented
    // protocol zero-space is 490us, but it is emitted slightly short (440us) so
    // that real receivers — which shift the mark/space boundary and lengthen the
    // received space — keep the recovered zero-space clear of IRremoteESP8266's
    // tight default ceiling (490-kMarkExcess=440, x1.25 = 551us). Our own decoder
    // (wide tolerance) and normal-bias receivers still recover it cleanly.
    inline constexpr AcTiming kTiming = {
        /*header_mark_us=*/4400,
        /*header_space_us=*/4300,
        /*bit_mark_us=*/580,
        /*zero_space_us=*/440,
        /*one_space_us=*/1600,
        /*trailer_mark_us=*/580,
        /*frame_gap_us=*/7400,
        /*tol_pct=*/40, // wide decode window: tolerate short 3rd-party bit marks + rx skew; integrity via signature/checksum (decode-only, unused when encoding)
        /*lsb_first=*/false,
    };

    inline constexpr size_t kBytes = 9;

    inline constexpr uint8_t kSig0 = 0xF2;
    inline constexpr uint8_t kSig1 = 0x0D; // ~0xF2

    inline constexpr size_t kOffTempSwing = 5; // byte 5: swing bits 0-2, temp bits 4-7
    inline constexpr size_t kOffModeFan = 6;   // byte 6: mode bits 0-2, fan bits 5-7
    inline constexpr uint8_t kModeOff = 7;     // Mode field value meaning power off
    inline constexpr uint8_t kMinTempC = 17;
    inline constexpr uint8_t kMaxTempC = 30;

    // The fixed framing prefix of a 9-byte standard frame (signature + inverted
    // pair + length/flags). bytes 0..4 are constant; bytes 5..7 hold the state;
    // byte 8 is the checksum.
    inline constexpr uint8_t kPrefix[5] = {0xF2, 0x0D, 0x03, 0xFC, 0x01};

    // XOR of all bytes except the last, stored in the last byte.
    inline uint8_t calcChecksum(const uint8_t *b)
    {
      uint8_t x = 0;
      for (size_t i = 0; i < kBytes - 1; ++i)
        x ^= b[i];
      return x;
    }
    inline bool checksumOk(const uint8_t *b)
    {
      return b[kBytes - 1] == calcChecksum(b);
    }

    inline Mode codeToMode(uint8_t c)
    {
      switch (c)
      {
      case 1: return Mode::COOL;
      case 2: return Mode::DRY;
      case 3: return Mode::HEAT;
      case 4: return Mode::FAN;
      default: return Mode::AUTO; // 0 and any unknown (incl. off=7)
      }
    }
    inline Fan codeToFan(uint8_t c)
    {
      switch (c)
      {
      case 2: return Fan::MIN_SPEED;
      case 3: return Fan::LOW_SPEED;
      case 4: return Fan::MED_SPEED;
      case 5: return Fan::HIGH_SPEED;
      case 6: return Fan::MAX_SPEED;
      default: return Fan::AUTO; // 0, unused 1, and any unknown
      }
    }
  } // namespace detail

  struct Frame
  {
    static constexpr size_t kBytes = detail::kBytes; // 9
    static constexpr size_t kMaxTicks = 320;         // two repeats: (header + 72 bits + trailer + gap) x2

    // raw state: a known-good frame (power on / Auto / 22C / fan auto) matching
    // IRToshibaAC's reset state, checksum precomputed.
    uint8_t bytes[kBytes] = {0xF2, 0x0D, 0x03, 0xFC, 0x01, 0x50, 0x00, 0x00, 0x51};
    uint16_t byte_length = 0;
    bool checksum_ok = false;

    // Power lives in the Mode field (off == 7); this remembers the mode to restore
    // when power is toggled back on.
    Mode last_mode_ = Mode::AUTO;

    // Mode field is byte 6 bits 0-2 (also the power indicator: 7 == off).
    void setModeField(uint8_t m)
    {
      bytes[detail::kOffModeFan] =
          static_cast<uint8_t>((bytes[detail::kOffModeFan] & ~0x07u) | (m & 0x07u));
    }

    bool power() const { return (bytes[detail::kOffModeFan] & 0x07u) != detail::kModeOff; }
    void setPower(bool on)
    {
      setModeField(on ? static_cast<uint8_t>(last_mode_) : detail::kModeOff);
    }

    Mode mode() const
    {
      const uint8_t m = bytes[detail::kOffModeFan] & 0x07u;
      return (m == detail::kModeOff) ? last_mode_ : detail::codeToMode(m);
    }
    void setMode(Mode m)
    {
      last_mode_ = m;
      setModeField(static_cast<uint8_t>(m));
    }

    // Whole-degree temperature, byte 5 high nibble = °C - 17, clamped 17..30.
    uint8_t temperatureC() const
    {
      return static_cast<uint8_t>((bytes[detail::kOffTempSwing] >> 4) + detail::kMinTempC);
    }
    void setTemperatureC(uint8_t c)
    {
      if (c < detail::kMinTempC) c = detail::kMinTempC;
      if (c > detail::kMaxTempC) c = detail::kMaxTempC;
      bytes[detail::kOffTempSwing] = static_cast<uint8_t>(
          (bytes[detail::kOffTempSwing] & 0x0Fu) | ((c - detail::kMinTempC) << 4));
    }

    Fan fan() const { return detail::codeToFan((bytes[detail::kOffModeFan] >> 5) & 0x07u); }
    void setFan(Fan f)
    {
      bytes[detail::kOffModeFan] = static_cast<uint8_t>(
          (bytes[detail::kOffModeFan] & ~(0x07u << 5)) | ((static_cast<uint8_t>(f) & 0x07u) << 5));
    }

    void printTo(Print &out) const
    {
      printAcSummary(out, "Toshiba", power(), toString(mode()),
                     static_cast<float>(temperatureC()), toString(fan()), bytes,
                     byte_length, checksum_ok);
    }

    // Editable setter template: copy-paste C++ that rebuilds this frame through the
    // logical setters. Lossy (the filter bit and the swing short-message are not
    // modelled here), so it is not bit-exact (use fromBytes for that).
    void printSetterSnippet(Print &out) const
    {
      out.println("// send code (Toshiba AC, editable -- lossy: filter/swing use defaults):");
      out.println("esp32irpk::ac::Toshiba::Frame f;");
      out.print("f.setPower(");
      out.print(power() ? "true" : "false");
      out.println(");");
      out.print("f.setMode(esp32irpk::ac::Toshiba::Mode::");
      out.print(toString(mode()));
      out.println(");");
      out.print("f.setTemperatureC(");
      out.print((unsigned)temperatureC());
      out.println(");");
      out.print("f.setFan(esp32irpk::ac::Toshiba::Fan::");
      out.print(toString(fan()));
      out.println(");");
      out.println("esp32irpk::ac::send(tx, f);");
    }

    // RAW ticks -> state bytes. false if not a standard Toshiba frame; checksum
    // validity is reported separately via `checksum_ok`. MSB-first.
    static bool fromRaw(const esp32irpk::IRRawTickView &raw, Frame &out)
    {
      out = Frame{};
      if (!raw.ticks)
        return false;
      size_t pos = 0;
      uint8_t b[detail::kBytes] = {};
      if (rawFrameToBytes(raw, pos, detail::kTiming, b, detail::kBytes) != detail::kBytes * 8)
        return false;
      // Fixed signature gate.
      if (b[0] != detail::kSig0 || b[1] != detail::kSig1)
        return false;
      for (size_t i = 0; i < detail::kBytes; ++i)
        out.bytes[i] = b[i];
      out.byte_length = kBytes;
      out.checksum_ok = detail::checksumOk(out.bytes);
      const uint8_t m = out.bytes[detail::kOffModeFan] & 0x07u;
      if (m != detail::kModeOff)
        out.last_mode_ = detail::codeToMode(m);
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
      const uint8_t m = out.bytes[detail::kOffModeFan] & 0x07u;
      if (m != detail::kModeOff)
        out.last_mode_ = detail::codeToMode(m);
      return true;
    }

    // state -> RAW ticks. Rewrites the fixed framing prefix (signature + inverted
    // pair + length/flags) and recomputes the XOR checksum, then renders the
    // MSB-first 9-byte frame TWICE. The standard TOSHIBA_AC protocol (and
    // IRremoteESP8266's sender, kToshibaACMinRepeat == kSingleRepeat) transmits
    // the message twice separated by the frame gap. Their matchGeneric decoder
    // needs that inter-frame gap as a bounded footer space; a single copy leaves
    // the receiver one rawbuf entry short and decodes as UNKNOWN.
    bool toRaw(esp32irpk::IRRawTickBuffer &out) const
    {
      uint8_t buf[kBytes];
      for (size_t i = 0; i < kBytes; ++i)
        buf[i] = bytes[i];
      for (size_t i = 0; i < sizeof(detail::kPrefix); ++i)
        buf[i] = detail::kPrefix[i];
      buf[kBytes - 1] = detail::calcChecksum(buf);

      out.len = 0;
      if (!bytesFrameToRaw(buf, kBytes * 8, detail::kTiming, out))
        return false;
      return bytesFrameToRaw(buf, kBytes * 8, detail::kTiming, out);
    }
  };

} // namespace esp32irpk::ac::Toshiba
