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
// Gree format. This one 8-byte wire format has model variants (SPEC §11.2,
// "two axes of variation"); they are carried as a `Model` parameter on the
// Frame, not as separate types. The implemented model is YBOFB: the model bit
// (byte 2, bit 6) stays clear, so byte 2 is a stable 0x20 and does not change
// with power. The logical field map (byte/bit positions and the mode/fan codes)
// is verified field-for-field against IRremoteESP8266's IRGreeAC (YBOFB model)
// via the compat_matrix_ac studies (gree_irremoteesp8266_*). Model::YAW1F and
// Model::YX1FSF are reserved for later (no field handling yet).
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

  // Vertical swing (byte 4 low nibble). The value set splits into "auto" modes
  // (the unit keeps moving toward a target) and fixed positions; selecting an
  // *_AUTO value also asserts the byte-0 SwingAuto bit, so the two stay
  // consistent and an illegal combination cannot be expressed.
  enum class SwingV : uint8_t
  {
    LAST_POS = 0,    // resume last position
    AUTO = 1,        // full sweep
    UP = 2,
    MIDDLE_UP = 3,
    MIDDLE = 4,
    MIDDLE_DOWN = 5,
    DOWN = 6,
    DOWN_AUTO = 7,   // sweep within the lower range
    MIDDLE_AUTO = 9, // sweep within the middle range
    UP_AUTO = 11,    // sweep within the upper range
  };

  // Horizontal swing (byte 4 bits 4-6). OFF holds, AUTO sweeps, the rest are
  // fixed positions from the far left to the far right.
  enum class SwingH : uint8_t
  {
    OFF = 0,
    AUTO = 1,
    MAX_LEFT = 2,
    LEFT = 3,
    MIDDLE = 4,
    RIGHT = 5,
    MAX_RIGHT = 6,
  };

  // Model variants of the one Gree 8-byte wire format (SPEC §11.2). They share
  // this format and differ only by a model marker / a few fields, so they are a
  // parameter on Frame rather than separate types. Only YBOFB is implemented;
  // YAW1F/YX1FSF are reserved (fromRaw will classify them, decode is future).
  enum class Model : uint8_t
  {
    YBOFB = 0,
    YAW1F,
    YX1FSF,
  };

  // Enum-to-name helpers for logging/printf. Return the enumerator name; unknown
  // values return "?".
  inline const char *toString(Mode m)
  {
    switch (m)
    {
    case Mode::AUTO: return "AUTO";
    case Mode::COOL: return "COOL";
    case Mode::HEAT: return "HEAT";
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
    case Fan::MIN_SPEED: return "MIN_SPEED";
    case Fan::MED_SPEED: return "MED_SPEED";
    case Fan::MAX_SPEED: return "MAX_SPEED";
    }
    return "?";
  }
  inline const char *toString(SwingV v)
  {
    switch (v)
    {
    case SwingV::LAST_POS: return "LAST_POS";
    case SwingV::AUTO: return "AUTO";
    case SwingV::UP: return "UP";
    case SwingV::MIDDLE_UP: return "MIDDLE_UP";
    case SwingV::MIDDLE: return "MIDDLE";
    case SwingV::MIDDLE_DOWN: return "MIDDLE_DOWN";
    case SwingV::DOWN: return "DOWN";
    case SwingV::DOWN_AUTO: return "DOWN_AUTO";
    case SwingV::MIDDLE_AUTO: return "MIDDLE_AUTO";
    case SwingV::UP_AUTO: return "UP_AUTO";
    }
    return "?";
  }
  inline const char *toString(SwingH v)
  {
    switch (v)
    {
    case SwingH::OFF: return "OFF";
    case SwingH::AUTO: return "AUTO";
    case SwingH::MAX_LEFT: return "MAX_LEFT";
    case SwingH::LEFT: return "LEFT";
    case SwingH::MIDDLE: return "MIDDLE";
    case SwingH::RIGHT: return "RIGHT";
    case SwingH::MAX_RIGHT: return "MAX_RIGHT";
    }
    return "?";
  }

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

    inline constexpr uint8_t kSwingAutoBit = 0x40; // byte 0 bit 6

    // Vertical-swing values that pair with the byte-0 SwingAuto bit.
    inline bool swingVIsAuto(Gree::SwingV v)
    {
      return v == Gree::SwingV::AUTO || v == Gree::SwingV::DOWN_AUTO ||
             v == Gree::SwingV::MIDDLE_AUTO || v == Gree::SwingV::UP_AUTO;
    }

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
    Model model = Model::YBOFB; // set by fromRaw; honored by toRaw/accessors

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
    // Vertical swing (byte 4 low nibble). setSwingV also keeps the byte-0
    // SwingAuto bit consistent with the chosen value (set for the *_AUTO modes).
    SwingV swingV() const { return static_cast<SwingV>(bytes[4] & 0x0Fu); }
    void setSwingV(SwingV v)
    {
      bytes[4] = static_cast<uint8_t>((bytes[4] & 0xF0u) | (static_cast<uint8_t>(v) & 0x0Fu));
      bytes[0] = static_cast<uint8_t>(
          (bytes[0] & ~detail::kSwingAutoBit) |
          (detail::swingVIsAuto(v) ? detail::kSwingAutoBit : 0));
    }
    // Horizontal swing (byte 4 bits 4-6).
    SwingH swingH() const { return static_cast<SwingH>((bytes[4] >> 4) & 0x07u); }
    void setSwingH(SwingH v)
    {
      bytes[4] = static_cast<uint8_t>((bytes[4] & ~0x70u) | (static_cast<uint8_t>(v) << 4));
    }

    // Human-readable dump for diagnostics: the shared summary plus Gree's swing
    // fields. Enum fields print as their raw code; the hex line shows every byte.
    void printTo(Print &out) const
    {
      printAcSummary(out, "Gree", power(), toString(mode()),
                     static_cast<float>(temperatureC()),
                     toString(fan()), bytes, byte_length, checksum_ok);
      out.print("//   swingV=");
      out.print(toString(swingV()));
      out.print(" swingH=");
      out.println(toString(swingH()));
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
      // Only YBOFB is implemented, so the model is YBOFB (already the default
      // from `out = Frame{}`). When YAW1F/YX1FSF are added, classify them here
      // from the model marker and decode their field deltas accordingly.
      out.model = Model::YBOFB;
      out.checksum_ok = (((out.bytes[7] >> 4) & 0x0Fu) == detail::checksum(out.bytes));
      return true;
    }

    // Decoded state bytes -> Frame, without going through RAW ticks. The
    // compact, bit-exact counterpart to fromRaw: copies the `kBytes` state and
    // reports checksum validity via `checksum_ok` (it does not reject a bad
    // checksum). `len` must equal `kBytes`. The implemented model is YBOFB.
    static bool fromBytes(const uint8_t *state, size_t len, Frame &out)
    {
      out = Frame{};
      if (!state || len != kBytes)
        return false;
      for (size_t i = 0; i < kBytes; ++i)
        out.bytes[i] = state[i];
      out.byte_length = kBytes;
      out.model = Model::YBOFB;
      out.checksum_ok = (((out.bytes[7] >> 4) & 0x0Fu) == detail::checksum(out.bytes));
      return true;
    }

    // state -> RAW ticks. Recomputes the checksum, so a frame built from setters
    // renders a valid two-block burst.
    bool toRaw(esp32irpk::IRRawTickBuffer &out) const
    {
      // Only YBOFB is implemented; refuse to encode a model whose field map is
      // not implemented rather than silently emitting a YBOFB-shaped frame.
      if (model != Model::YBOFB)
        return false;

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
