#pragma once

#include "../ESP32IRPulseKit.h"
#include "AcCodec.h"

// Hitachi air-conditioner support (the 28-byte HITACHI_AC protocol, RAS-/RAK-
// series remotes). A full setting is a single 28-byte pulse-distance frame,
// MSB-first, beginning with a fixed 9-byte framing prefix, with a sum-based
// checksum in the last byte. See SPEC §11.2.
//
// Unusually, the logical fields are stored BIT-REVERSED within their byte (the
// remote sends each field LSB-first inside an MSB-first byte stream), and the
// mode/fan/temperature fields are coupled: Fan mode carries a sentinel temperature,
// Dry mode limits the fan range, and changing the mode re-clamps the fan. This
// implementation mirrors IRremoteESP8266's IRHitachiAc byte-for-byte so an
// independent stack round-trips it; the field map is verified against IRHitachiAc
// via the compat_matrix_ac studies (hitachi_irremoteesp8266_*).
//
// This targets the 28-byte HITACHI_AC format (no `Model` parameter). The other
// Hitachi sizes (13 / 27 / 33 / 37 / 43 / 53-byte) are different frames and would be
// separate Frame types (SPEC §11.2). Timers and the comfort flags are not settable.

namespace esp32irpk::ac::Hitachi
{

  // Per-vendor enums; values are the wire codes (before the per-byte bit reversal).
  // Mode lives in byte 10, Fan in byte 13.
  enum class Mode : uint8_t
  {
    AUTO = 2,
    HEAT = 3,
    COOL = 4,
    DRY = 5,
    FAN = 0xC,
  };

  // Fan wire codes are non-contiguous (no 4). Enum values ARE the wire codes.
  enum class Fan : uint8_t
  {
    AUTO = 1,
    LOW_SPEED = 2,
    MED_SPEED = 3,
    HIGH_SPEED = 5,
  };

  inline const char *toString(Mode m)
  {
    switch (m)
    {
    case Mode::AUTO: return "AUTO";
    case Mode::HEAT: return "HEAT";
    case Mode::COOL: return "COOL";
    case Mode::DRY: return "DRY";
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
    // Standard HITACHI_AC timing (kHitachiAcFreq = 38kHz). MSB-first.
    inline constexpr AcTiming kTiming = {
        /*header_mark_us=*/3300,
        /*header_space_us=*/1700,
        /*bit_mark_us=*/400,
        /*zero_space_us=*/500,
        /*one_space_us=*/1250,
        /*trailer_mark_us=*/400,
        /*frame_gap_us=*/20000, // sent once; real trailing gap (kDefaultMessageGap) is never captured
        /*tol_pct=*/40,         // wide decode window (see [[ac-decode-tolerance-loose]])
        /*lsb_first=*/false,    // MSB-first byte stream (fields are bit-reversed inside)
    };

    inline constexpr size_t kBytes = 28;

    // Fixed 9-byte framing prefix (from IRHitachiAc::stateReset). byte 9 is
    // temperature-dependent (0x10 normally, 0x90 at the minimum temperature).
    inline constexpr uint8_t kPrefix[9] = {0x80, 0x08, 0x0C, 0x02, 0xFD, 0x80, 0x7F, 0x88, 0x48};

    inline constexpr size_t kOffTempFlag = 9; // 0x10, or 0x90 at min temp
    inline constexpr size_t kOffMode = 10;
    inline constexpr size_t kOffTemp = 11;
    inline constexpr size_t kOffFan = 13;
    inline constexpr size_t kOffSwingV = 14; // bit 7 (low bits fixed 0x60)
    inline constexpr size_t kOffSwingH = 15; // bit 7 (low bits fixed 0x60)
    inline constexpr size_t kOffPower = 17;  // bit 0
    inline constexpr size_t kOffFixed24 = 24;
    inline constexpr size_t kOffSum = 27;

    inline constexpr uint8_t kSwingBase = 0x60; // low bits of bytes 14/15
    inline constexpr uint8_t kFixed24 = 0x80;
    inline constexpr uint8_t kMinTempC = 16;
    inline constexpr uint8_t kMaxTempC = 32;
    inline constexpr uint8_t kFanTemp = 64; // sentinel "no temperature" (Fan mode)

    inline uint8_t reverseBits8(uint8_t b)
    {
      b = static_cast<uint8_t>(((b & 0xF0u) >> 4) | ((b & 0x0Fu) << 4));
      b = static_cast<uint8_t>(((b & 0xCCu) >> 2) | ((b & 0x33u) << 2));
      b = static_cast<uint8_t>(((b & 0xAAu) >> 1) | ((b & 0x55u) << 1));
      return b;
    }

    // Checksum (IRHitachiAc::calcChecksum): 62 minus the bit-reversed value of every
    // byte except the last, then bit-reverse the result. Stored in the last byte.
    inline uint8_t calcChecksum(const uint8_t *b)
    {
      uint8_t sum = 62;
      for (size_t i = 0; i < kBytes - 1; ++i)
        sum = static_cast<uint8_t>(sum - reverseBits8(b[i]));
      return reverseBits8(sum);
    }
    inline void writeChecksum(uint8_t *b) { b[kOffSum] = calcChecksum(b); }
    inline bool checksumOk(const uint8_t *b) { return b[kOffSum] == calcChecksum(b); }

    inline Mode codeToMode(uint8_t c)
    {
      switch (c)
      {
      case 3: return Mode::HEAT;
      case 4: return Mode::COOL;
      case 5: return Mode::DRY;
      case 0xC: return Mode::FAN;
      default: return Mode::AUTO; // 2 and any unknown
      }
    }
    inline Fan codeToFan(uint8_t c)
    {
      switch (c)
      {
      case 2: return Fan::LOW_SPEED;
      case 3: return Fan::MED_SPEED;
      case 5: return Fan::HIGH_SPEED;
      default: return Fan::AUTO; // 1 and any unknown
      }
    }
  } // namespace detail

  struct Frame
  {
    static constexpr size_t kBytes = detail::kBytes; // 28
    static constexpr size_t kMaxTicks = 2 + 28 * 8 * 2 + 2 + 6; // header + 224 bits + trailer

    // raw state: a known-good frame (power on / cool / 23C / fan auto / no swing),
    // matching IRHitachiAc's encoding, checksum precomputed.
    uint8_t bytes[kBytes] = {0x80, 0x08, 0x0C, 0x02, 0xFD, 0x80, 0x7F, 0x88, 0x48, 0x10,
                             0x20, 0x74, 0x00, 0x80, 0x60, 0x60, 0x00, 0x01, 0x00, 0x00,
                             0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x28};
    uint16_t byte_length = 0;
    bool checksum_ok = false;

    // Fan mode carries a sentinel temperature; changing the mode restores this.
    uint8_t previous_temp_ = 23;

    bool power() const { return (bytes[detail::kOffPower] & 0x01u) != 0; }
    void setPower(bool on)
    {
      bytes[detail::kOffPower] =
          static_cast<uint8_t>((bytes[detail::kOffPower] & ~0x01u) | (on ? 1u : 0u));
    }

    // Temperature is stored bit-reversed as (°C << 1); byte 9 flags the minimum.
    uint8_t temperatureC() const
    {
      return static_cast<uint8_t>(detail::reverseBits8(bytes[detail::kOffTemp]) >> 1);
    }
    void setTemperatureC(uint8_t c)
    {
      uint8_t t;
      if (c != detail::kFanTemp)
      {
        previous_temp_ = c;
        if (c < detail::kMinTempC) c = detail::kMinTempC;
        if (c > detail::kMaxTempC) c = detail::kMaxTempC;
        t = c;
      }
      else
      {
        t = detail::kFanTemp;
      }
      bytes[detail::kOffTemp] = detail::reverseBits8(static_cast<uint8_t>(t << 1));
      bytes[detail::kOffTempFlag] = (t == detail::kMinTempC) ? 0x90 : 0x10;
    }

    Mode mode() const { return detail::codeToMode(detail::reverseBits8(bytes[detail::kOffMode])); }
    void setMode(Mode m)
    {
      const uint8_t code = static_cast<uint8_t>(m);
      if (m == Mode::FAN)
        setTemperatureC(detail::kFanTemp); // Fan mode uses the sentinel temperature
      bytes[detail::kOffMode] = detail::reverseBits8(code);
      if (m != Mode::FAN)
        setTemperatureC(previous_temp_);
      setFan(fan()); // re-clamp the fan to the new mode's range
    }

    Fan fan() const { return detail::codeToFan(detail::reverseBits8(bytes[detail::kOffFan])); }
    void setFan(Fan f)
    {
      uint8_t fanmin = static_cast<uint8_t>(Fan::AUTO);
      uint8_t fanmax = static_cast<uint8_t>(Fan::HIGH_SPEED);
      switch (mode())
      {
      case Mode::DRY: // only 2 low speeds in Dry
        fanmin = static_cast<uint8_t>(Fan::LOW_SPEED);
        fanmax = static_cast<uint8_t>(Fan::LOW_SPEED) + 1;
        break;
      case Mode::FAN: // no Auto in Fan mode
        fanmin = static_cast<uint8_t>(Fan::LOW_SPEED);
        break;
      default:
        break;
      }
      uint8_t speed = static_cast<uint8_t>(f);
      if (speed < fanmin) speed = fanmin;
      if (speed > fanmax) speed = fanmax;
      bytes[detail::kOffFan] = detail::reverseBits8(speed);
    }

    bool swingV() const { return (bytes[detail::kOffSwingV] & 0x80u) != 0; }
    void setSwingV(bool on)
    {
      bytes[detail::kOffSwingV] =
          static_cast<uint8_t>(detail::kSwingBase | (on ? 0x80u : 0x00u));
    }
    bool swingH() const { return (bytes[detail::kOffSwingH] & 0x80u) != 0; }
    void setSwingH(bool on)
    {
      bytes[detail::kOffSwingH] =
          static_cast<uint8_t>(detail::kSwingBase | (on ? 0x80u : 0x00u));
    }

    void printTo(Print &out) const
    {
      printAcSummary(out, "Hitachi", power(), toString(mode()),
                     static_cast<float>(temperatureC()), toString(fan()), bytes,
                     byte_length, checksum_ok);
      out.print("// swingV=");
      out.print(swingV() ? "on" : "off");
      out.print(" swingH=");
      out.println(swingH() ? "on" : "off");
    }

    void printSetterSnippet(Print &out) const
    {
      out.println("// send code (Hitachi AC, editable -- lossy: timers/comfort flags use defaults):");
      out.println("esp32irpk::ac::Hitachi::Frame f;");
      out.print("f.setPower(");
      out.print(power() ? "true" : "false");
      out.println(");");
      out.print("f.setMode(esp32irpk::ac::Hitachi::Mode::");
      out.print(toString(mode()));
      out.println(");");
      out.print("f.setTemperatureC(");
      out.print((unsigned)temperatureC());
      out.println(");");
      out.print("f.setFan(esp32irpk::ac::Hitachi::Fan::");
      out.print(toString(fan()));
      out.println(");");
      out.print("f.setSwingV(");
      out.print(swingV() ? "true" : "false");
      out.println(");");
      out.println("esp32irpk::ac::send(tx, f);");
    }

    // RAW ticks -> state bytes. false if not a standard HITACHI_AC frame; checksum
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
      // Fixed 9-byte framing-prefix gate.
      for (size_t i = 0; i < sizeof(detail::kPrefix); ++i)
        if (b[i] != detail::kPrefix[i])
          return false;
      for (size_t i = 0; i < detail::kBytes; ++i)
        out.bytes[i] = b[i];
      out.byte_length = kBytes;
      out.checksum_ok = detail::checksumOk(out.bytes);
      const uint8_t t = out.temperatureC();
      if (out.mode() != Mode::FAN && t >= detail::kMinTempC && t <= detail::kMaxTempC)
        out.previous_temp_ = t;
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
      const uint8_t t = out.temperatureC();
      if (out.mode() != Mode::FAN && t >= detail::kMinTempC && t <= detail::kMaxTempC)
        out.previous_temp_ = t;
      return true;
    }

    // state -> RAW ticks. Forces the fixed framing prefix + byte 24, recomputes the
    // checksum, then renders the MSB-first 28-byte frame once (kNoRepeat).
    bool toRaw(esp32irpk::IRRawTickBuffer &out) const
    {
      uint8_t buf[kBytes];
      for (size_t i = 0; i < kBytes; ++i)
        buf[i] = bytes[i];
      for (size_t i = 0; i < sizeof(detail::kPrefix); ++i)
        buf[i] = detail::kPrefix[i];
      buf[detail::kOffFixed24] = detail::kFixed24;
      detail::writeChecksum(buf);

      out.len = 0;
      return bytesFrameToRaw(buf, kBytes * 8, detail::kTiming, out);
    }
  };

} // namespace esp32irpk::ac::Hitachi
