#pragma once

#include "../ESP32IRPulseKit.h"
#include "AcCodec.h"

// Fujitsu air-conditioner support (the "Fujitsu AC" protocol used by the common
// AR-series remotes). A full setting is a 16-byte pulse-distance "long" frame; a
// power-off (and other standalone commands) is a 7-byte "short" frame. Each is
// sent once (no repeat). See SPEC §11.2.
//
// Frame mechanics (timing, signature, checksum) follow the documented format.
// The logical field map (byte/bit positions and the mode/fan/swing codes) is
// verified field-for-field against IRremoteESP8266's IRFujitsuAC via the
// compat_matrix_ac studies (fujitsu_irremoteesp8266_*).
//
// This implementation targets a single model, ARRAH2E (IRremoteESP8266's default
// fujitsu_ac_remote_model_t::ARRAH2E), so there is no `Model` parameter yet. The
// other Fujitsu models (ARDB1 / ARJW2 / ARREB1E / ARRY4 / ARREW4E) differ in
// state length, the byte-5 marker, the checksum complement, and (for ARREW4E)
// the temperature encoding; they are reserved for later and not handled here
// (SPEC §11.2, "two axes of variation").
//
// Long vs short frame:
//   * power ON  -> 16-byte long frame, byte 5 = 0xFE, checksum in byte 15.
//   * power OFF -> 7-byte short frame  {14 63 00 10 10 02 FD}; byte 6 = ~byte 5.
// Power is carried by the frame type, not a state bit (the byte-8 Power bit stays
// 0 in long frames). A short OFF frame carries no mode/temp/fan — those fields are
// vendor don't-cares on power-off, so decoding one keeps the template defaults and
// only reports power=off.

namespace esp32irpk::ac::Fujitsu
{

  // Per-vendor enums: only the values this protocol supports. Values match the
  // wire codes directly (Auto=0, Cool=1, ...), so the enum doubles as the code.
  enum class Mode : uint8_t
  {
    AUTO = 0,
    COOL = 1,
    DRY = 2,
    FAN = 3,
    HEAT = 4,
  };

  // Arduino defines LOW/HIGH as macros, so speed names carry the _SPEED suffix.
  // Fujitsu exposes auto, three fan speeds, and a quiet step.
  enum class Fan : uint8_t
  {
    AUTO = 0,
    HIGH_SPEED = 1,
    MED_SPEED = 2,
    LOW_SPEED = 3,
    QUIET = 4,
  };

  // Airflow swing (byte 10 bits 4-5). OFF holds the vanes; VERTICAL/HORIZONTAL
  // sweep one axis; BOTH sweeps both. Whether a given unit honours HORIZONTAL is
  // hardware-dependent.
  enum class Swing : uint8_t
  {
    OFF = 0,
    VERTICAL = 1,
    HORIZONTAL = 2,
    BOTH = 3,
  };

  // Enum-to-name helpers for logging/printf. Return the enumerator name; unknown
  // values return "?".
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
    case Fan::HIGH_SPEED: return "HIGH_SPEED";
    case Fan::MED_SPEED: return "MED_SPEED";
    case Fan::LOW_SPEED: return "LOW_SPEED";
    case Fan::QUIET: return "QUIET";
    }
    return "?";
  }
  inline const char *toString(Swing s)
  {
    switch (s)
    {
    case Swing::OFF: return "OFF";
    case Swing::VERTICAL: return "VERTICAL";
    case Swing::HORIZONTAL: return "HORIZONTAL";
    case Swing::BOTH: return "BOTH";
    }
    return "?";
  }

  namespace detail
  {
    // Documented Fujitsu AC pulse-distance timing.
    inline constexpr AcTiming kTiming = {
        /*header_mark_us=*/3324,
        /*header_space_us=*/1574,
        /*bit_mark_us=*/448,
        /*zero_space_us=*/390,
        /*one_space_us=*/1182,
        /*trailer_mark_us=*/448,
        /*frame_gap_us=*/8100,
        /*tol_pct=*/40, // wide decode window: 3rd-party encoders emit short bit marks (HeatpumpIR 410us) + the rx skews marks shorter; integrity is via signature/checksum, not tight per-bit windows (decode-only, unused when encoding)
        /*lsb_first=*/true,
    };

    inline constexpr size_t kBytes = 16;      // long frame
    inline constexpr size_t kShortBytes = 7;  // power-off / short command frame

    // Fixed bytes common to every ARRAH2E frame. Byte 2 is a device id (normally
    // 0) and is not checked. Byte 5 is the long-frame marker (0xFE); byte 6 is the
    // "bytes remaining after this one" length; byte 7 is the protocol version.
    inline constexpr uint8_t kSig0 = 0x14;
    inline constexpr uint8_t kSig1 = 0x63;
    inline constexpr uint8_t kSig3 = 0x10;
    inline constexpr uint8_t kSig4 = 0x10;
    inline constexpr uint8_t kLongMarker = 0xFE;   // byte 5 in a long frame
    inline constexpr uint8_t kRestLength = 0x09;   // byte 6 = kBytes - 7
    inline constexpr uint8_t kProtocol = 0x30;     // byte 7 (ARRAH2E)
    inline constexpr uint8_t kCmdTurnOff = 0x02;   // byte 5 in the short OFF frame

    // Byte offsets / masks for the logical fields (long frame).
    inline constexpr size_t kOffTemp = 8;     // bit 0 Power, bit 1 Fahrenheit, bits 2-7 Temp
    inline constexpr uint8_t kTempLowMask = 0x03; // byte 8 bits 0-1 (power/fahrenheit) preserved
    inline constexpr size_t kOffMode = 9;     // bits 0-2
    inline constexpr size_t kOffFan = 10;     // bits 0-2 fan, bits 4-5 swing
    inline constexpr size_t kOffChecksum = 15;

    inline constexpr uint8_t kMinTempC = 16;
    inline constexpr uint8_t kMaxTempC = 30;

    // Long-frame checksum: byte 15 = -(sum of bytes 7..14) (mod 256).
    inline uint8_t checksum(const uint8_t *b)
    {
      uint16_t sum = 0;
      for (size_t i = 7; i <= 14; ++i)
        sum += b[i];
      return static_cast<uint8_t>((0x100u - (sum & 0xFFu)) & 0xFFu);
    }
  } // namespace detail

  struct Frame
  {
    static constexpr size_t kBytes = detail::kBytes; // 16
    static constexpr size_t kMaxTicks = 300;         // long frame: header + 128 bits + trailer

    // raw state: a known-good long frame (Power on / Cool / 24C / fan High /
    // swing Both) matching IRFujitsuAC's reset state for ARRAH2E. Setters touch
    // only the logical fields; toRaw rewrites the fixed bytes and checksum.
    uint8_t bytes[kBytes] = {
        0x14, 0x63, 0x00, 0x10, 0x10, 0xFE, 0x09, 0x30,
        0x80, 0x01, 0x31, 0x00, 0x00, 0x00, 0x00, 0x1E};
    uint16_t byte_length = 0;
    bool checksum_ok = false;
    // Power is the long-vs-short frame selector, not a state bit. Defaults on so a
    // freshly built Frame sends its full state.
    bool power_on = true;

    // Logical accessors over `bytes` (layout per the file header).
    bool power() const { return power_on; }
    void setPower(bool on) { power_on = on; }

    Mode mode() const { return static_cast<Mode>(bytes[detail::kOffMode] & 0x07u); }
    void setMode(Mode m)
    {
      bytes[detail::kOffMode] = static_cast<uint8_t>(
          (bytes[detail::kOffMode] & ~0x07u) | (static_cast<uint8_t>(m) & 0x07u));
    }

    // Temperature: the 6-bit Temp field (byte 8 bits 2-7) holds (C - 16) * 4 for
    // this model, so the integer degrees land in the high nibble. Bits 0-1
    // (power/fahrenheit) are preserved.
    float temperatureC() const
    {
      uint8_t v = static_cast<uint8_t>((bytes[detail::kOffTemp] >> 2) & 0x3Fu);
      return static_cast<float>(v / 4 + detail::kMinTempC);
    }
    void setTemperatureC(float c)
    {
      if (c < detail::kMinTempC) c = static_cast<float>(detail::kMinTempC);
      if (c > detail::kMaxTempC) c = static_cast<float>(detail::kMaxTempC);
      uint8_t whole = static_cast<uint8_t>(c + 0.5f);
      uint8_t v = static_cast<uint8_t>((whole - detail::kMinTempC) * 4); // 6-bit Temp field
      bytes[detail::kOffTemp] = static_cast<uint8_t>(
          (bytes[detail::kOffTemp] & detail::kTempLowMask) | ((v & 0x3Fu) << 2));
    }

    Fan fan() const { return static_cast<Fan>(bytes[detail::kOffFan] & 0x07u); }
    void setFan(Fan f)
    {
      bytes[detail::kOffFan] = static_cast<uint8_t>(
          (bytes[detail::kOffFan] & ~0x07u) | (static_cast<uint8_t>(f) & 0x07u));
    }

    // Swing occupies byte 10 bits 4-5, sharing the byte with the fan (bits 0-2).
    Swing swing() const { return static_cast<Swing>((bytes[detail::kOffFan] >> 4) & 0x03u); }
    void setSwing(Swing s)
    {
      bytes[detail::kOffFan] = static_cast<uint8_t>(
          (bytes[detail::kOffFan] & ~(0x03u << 4)) | ((static_cast<uint8_t>(s) & 0x03u) << 4));
    }

    // Human-readable dump for diagnostics: the shared summary plus Fujitsu's swing
    // field. The hex line shows every long-frame byte.
    void printTo(Print &out) const
    {
      printAcSummary(out, "Fujitsu", power(), toString(mode()),
                     temperatureC(), toString(fan()), bytes,
                     byte_length, checksum_ok);
      out.print("//   swing=");
      out.println(toString(swing()));
    }

    // Editable setter template: copy-paste C++ that rebuilds this frame through
    // the logical setters, ready to tweak before sending. NOTE: lossy — fields
    // without a setter (timers, filter, clean) reset to template defaults, so it
    // is not bit-exact (use fromBytes for that). Enums come from toString.
    void printSetterSnippet(Print &out) const
    {
      out.println("// send code (Fujitsu AC, editable -- lossy: no-setter fields use defaults):");
      out.println("esp32irpk::ac::Fujitsu::Frame f;");
      out.print("f.setPower(");
      out.print(power() ? "true" : "false");
      out.println(");");
      out.print("f.setMode(esp32irpk::ac::Fujitsu::Mode::");
      out.print(toString(mode()));
      out.println(");");
      out.print("f.setTemperatureC(");
      out.print(temperatureC(), 1);
      out.println(");");
      out.print("f.setFan(esp32irpk::ac::Fujitsu::Fan::");
      out.print(toString(fan()));
      out.println(");");
      out.print("f.setSwing(esp32irpk::ac::Fujitsu::Swing::");
      out.print(toString(swing()));
      out.println(");");
      out.println("esp32irpk::ac::send(tx, f);");
    }

    // RAW ticks -> state bytes. false if not a Fujitsu ARRAH2E frame; checksum
    // validity is reported separately via `checksum_ok`. Decodes both the 16-byte
    // long frame (power on) and the 7-byte short frame (power off).
    static bool fromRaw(const esp32irpk::IRRawTickView &raw, Frame &out)
    {
      out = Frame{};
      size_t pos = 0;

      uint8_t b[detail::kBytes] = {};
      size_t bits = rawFrameToBytes(raw, pos, detail::kTiming, b, sizeof(b));
      // Common fixed signature for both frame lengths.
      if (b[0] != detail::kSig0 || b[1] != detail::kSig1 ||
          b[3] != detail::kSig3 || b[4] != detail::kSig4)
        return false;

      if (bits == detail::kBytes * 8)
      {
        // Long frame: require the ARRAH2E marker so other models don't match.
        if (b[5] != detail::kLongMarker)
          return false;
        for (size_t i = 0; i < detail::kBytes; ++i)
          out.bytes[i] = b[i];
        out.byte_length = kBytes;
        out.power_on = true;
        out.checksum_ok = (b[detail::kOffChecksum] == detail::checksum(b));
        return true;
      }
      if (bits == detail::kShortBytes * 8)
      {
        // Short frame: a standalone command (power off). Store the short bytes so
        // a byte-dump round-trips; keep the template state for the don't-care
        // mode/temp/fan fields.
        for (size_t i = 0; i < detail::kShortBytes; ++i)
          out.bytes[i] = b[i];
        out.byte_length = detail::kShortBytes;
        out.power_on = (b[5] != detail::kCmdTurnOff);
        out.checksum_ok = (b[detail::kShortBytes - 1] ==
                           static_cast<uint8_t>(~b[detail::kShortBytes - 2]));
        return true;
      }
      return false;
    }

    // Decoded state bytes -> Frame, without going through RAW ticks. The compact,
    // bit-exact counterpart to fromRaw: accepts a 16-byte long state or a 7-byte
    // short state and reports checksum validity via `checksum_ok` (it does not
    // reject a bad checksum).
    static bool fromBytes(const uint8_t *state, size_t len, Frame &out)
    {
      out = Frame{};
      if (!state)
        return false;
      if (len == kBytes)
      {
        for (size_t i = 0; i < kBytes; ++i)
          out.bytes[i] = state[i];
        out.byte_length = kBytes;
        out.power_on = true;
        out.checksum_ok = (out.bytes[detail::kOffChecksum] == detail::checksum(out.bytes));
        return true;
      }
      if (len == detail::kShortBytes)
      {
        for (size_t i = 0; i < detail::kShortBytes; ++i)
          out.bytes[i] = state[i];
        out.byte_length = detail::kShortBytes;
        out.power_on = (state[5] != detail::kCmdTurnOff);
        out.checksum_ok = (state[detail::kShortBytes - 1] ==
                           static_cast<uint8_t>(~state[detail::kShortBytes - 2]));
        return true;
      }
      return false;
    }

    // state -> RAW ticks. Power on renders the 16-byte long frame (fixed bytes and
    // checksum rewritten); power off renders the 7-byte short OFF frame. Each is
    // sent once, as a real remote does.
    bool toRaw(esp32irpk::IRRawTickBuffer &out) const
    {
      out.len = 0;
      if (power_on)
      {
        uint8_t buf[kBytes];
        for (size_t i = 0; i < kBytes; ++i)
          buf[i] = bytes[i];
        // Force the fixed long-frame bytes so a frame built from a short state (or
        // edited fields) still renders a valid long frame.
        buf[0] = detail::kSig0;
        buf[1] = detail::kSig1;
        buf[3] = detail::kSig3;
        buf[4] = detail::kSig4;
        buf[5] = detail::kLongMarker;
        buf[6] = detail::kRestLength;
        buf[7] = detail::kProtocol;
        buf[detail::kOffChecksum] = detail::checksum(buf);
        return bytesFrameToRaw(buf, kBytes * 8, detail::kTiming, out);
      }
      // Short OFF frame: signature + turn-off command + inverted-command checksum.
      uint8_t buf[detail::kShortBytes] = {
          detail::kSig0, detail::kSig1, 0x00, detail::kSig3, detail::kSig4,
          detail::kCmdTurnOff, static_cast<uint8_t>(~detail::kCmdTurnOff)};
      return bytesFrameToRaw(buf, detail::kShortBytes * 8, detail::kTiming, out);
    }
  };

} // namespace esp32irpk::ac::Fujitsu
