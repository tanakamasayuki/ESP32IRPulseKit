#pragma once

#include "../ESP32IRPulseKit.h"
#include "AcCodec.h"

// Panasonic air-conditioner support (Kaseikyo/AEHA family: two pulse-distance
// frames, LSB-first, frame 2 ends with a sum checksum). See SPEC §11.2.
//
// Frame mechanics (timing, two-frame layout, checksum) follow the documented
// Kaseikyo/Panasonic format. This wire format has model variants (SPEC §11.2,
// "two axes of variation"); they are carried as a `Model` parameter on the
// Frame, not separate types. JKE/DKE/NKE/LKE/RKR are handled — they share the
// power/mode/temperature/fan field map and differ only in fixed marker bytes
// (see detail::applyModelMarkers / detectModel). CKP is reserved (toggle power
// and relocated quiet/powerful bits make its semantics different). The
// logical field map (which byte/bit holds power/mode/temperature/fan, and the
// mode/fan codes) is verified byte-for-byte against IRremoteESP8266's
// IRPanasonicAc encoder (tests/studies/compat_matrix_ac/irremoteesp8266_tx).
// The separate Panasonic-AC32 protocol is a different wire format (a future
// distinct Frame type), not a model of this one.

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
  // Fan selector. The speeds (MIN..MAX) live in the fan-byte high nibble. QUIET
  // and POWERFUL are the comfort-mode buttons: they keep the fan nibble at auto
  // and set a separate byte21 flag, and are mutually exclusive with a speed — so
  // they are values of this one selector, not an independent field.
  enum class Fan : uint8_t
  {
    AUTO = 0,
    MIN_SPEED,  // weakest airflow
    LOW_SPEED,
    MED_SPEED,
    HIGH_SPEED,
    MAX_SPEED,  // strongest airflow
    QUIET,      // しずか: comfort mode (fan nibble stays auto, byte21 bit5)
    POWERFUL,   // パワフル: comfort mode (fan nibble stays auto, byte21 bit0)
  };

  // Vertical louver (swing) position. Stored in the low nibble of the fan byte
  // (the fan speed is the high nibble), so it is independent of the fan setting.
  // AUTO is the swing-everything sweep; P1..P5 are the fixed positions (P1 most
  // horizontal). Values are the on-air nibble.
  enum class Louver : uint8_t
  {
    AUTO = 0xF,
    P1 = 1,
    P2 = 2,
    P3 = 3,
    P4 = 4,
    P5 = 5,
  };

  // Model variants of the one Kaseikyo Panasonic-AC wire format (SPEC §11.2).
  // They share this format and differ only by a few marker bytes, so they are a
  // parameter on Frame rather than separate types. JKE (the default; template
  // byte-identical to IRPanasonicAc's known-good state), DKE, NKE, LKE and RKR
  // are handled: DKE sets byte23=0x01 / byte25=0x06 (+ horizontal swing in
  // byte17); NKE sets byte17=0x06; LKE sets byte17=0x06 and byte13 bit1; RKR
  // sets byte23=0x89 and byte13 bit3; JKE keeps byte23=0x81. CKP is reserved
  // (toggle power + relocated quiet/powerful bits); encoding it fails.
  enum class Model : uint8_t
  {
    JKE = 0,
    DKE,
    NKE,
    LKE,
    CKP,
    RKR,
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
    inline constexpr size_t kOffTemp = kFrame1Bytes + 6;     // temperature (integer, c<<1)
    inline constexpr size_t kOffFan = kFrame1Bytes + 8;      // fan nibble (high) + louver nibble (low)
    inline constexpr size_t kOffHalfDegree = kFrame1Bytes + 14; // byte22: +0.5C is bit7
    inline constexpr size_t kOffQuietPowerful = kFrame1Bytes + 13; // byte21: quiet/powerful flags
    inline constexpr uint8_t kQuietBit = 0x20;    // byte21 bit5
    inline constexpr uint8_t kPowerfulBit = 0x01; // byte21 bit0
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
    // Fan SPEED <-> fan-byte high nibble (min/low/med/high/max = 0x3..0x7, auto =
    // 0xA). QUIET/POWERFUL are not speeds: they ride on the auto nibble and a
    // byte21 flag, so they are handled in fan()/setFan(), not here.
    inline uint8_t fanToNibble(Fan f)
    {
      switch (f)
      {
      case Fan::MIN_SPEED: return 0x3;
      case Fan::LOW_SPEED: return 0x4;
      case Fan::MED_SPEED: return 0x5;
      case Fan::HIGH_SPEED: return 0x6;
      case Fan::MAX_SPEED: return 0x7;
      case Fan::AUTO: case Fan::QUIET: case Fan::POWERFUL: default: return 0xA;
      }
    }
    inline Fan nibbleToFan(uint8_t n)
    {
      switch (n)
      {
      case 0x3: return Fan::MIN_SPEED;
      case 0x4: return Fan::LOW_SPEED;
      case 0x5: return Fan::MED_SPEED;
      case 0x6: return Fan::HIGH_SPEED;
      case 0x7: return Fan::MAX_SPEED;
      default: return Fan::AUTO;
      }
    }

    // Model is carried in fixed marker bytes of frame 2 (overall indices 13, 17,
    // 21, 23, 25). The logical fields (power/mode/temperature/fan) sit elsewhere
    // and are identical across these models, so a model is a parameter, not a
    // separate Frame type. The marker byte values and the detection order mirror
    // the documented Panasonic-AC format (cf. IRPanasonicAc setModel/getModel).
    inline constexpr uint8_t kSwingHMiddle = 0x6; // DKE writes this into byte17 low nibble

    // Stamp the marker bytes for `m`, preserving the logical fields. byte13 holds
    // power (bit0) and mode (high nibble) alongside model bits 1..3, so only
    // those marker bits are touched there.
    inline void applyModelMarkers(uint8_t *b, Model m)
    {
      b[13] = static_cast<uint8_t>(b[13] & 0xF1u); // clear byte13 model bits 1..3
      b[17] = 0x00;
      b[21] = static_cast<uint8_t>(b[21] & ~0x10u); // clear the CKP-only marker bit
      b[23] = 0x81;
      b[25] = 0x00;
      switch (m)
      {
      case Model::DKE: b[23] = 0x01; b[25] = 0x06; b[17] = kSwingHMiddle; break;
      case Model::NKE: b[17] = 0x06; break;
      case Model::LKE: b[13] = static_cast<uint8_t>(b[13] | 0x02u); b[17] = 0x06; break;
      case Model::RKR: b[13] = static_cast<uint8_t>(b[13] | 0x08u); b[23] = 0x89; break;
      case Model::JKE: default: break; // JKE keeps the base markers
      }
    }

    // Classify the model from the marker bytes (detection order mirrors
    // IRPanasonicAc::getModel). The LKE marker shares byte13's low nibble with
    // the power bit (bit0), so the power bit is masked out (bits 1..3 only) —
    // otherwise a powered-on LKE frame would misclassify as NKE. An unrecognized
    // combination falls back to JKE (the base layout).
    inline Model detectModel(const uint8_t *b)
    {
      if (b[23] == 0x89) return Model::RKR;
      if (b[17] == 0x00)
      {
        if ((b[21] & 0x10u) && (b[23] & 0x01u)) return Model::CKP;
        if (b[23] & 0x80u) return Model::JKE;
      }
      if (b[17] == 0x06 && (b[13] & 0x0Eu) == 0x02u) return Model::LKE;
      if (b[23] == 0x01) return Model::DKE;
      if (b[17] == 0x06) return Model::NKE;
      return Model::JKE;
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
    Model model = Model::JKE; // set by fromRaw; honored by toRaw/accessors

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
    // Temperature supports 0.5C steps, stored as an integer part in the temp byte
    // (c<<1) plus a +0.5 bit in byte22 bit7. setTemperatureC() and temperatureC()
    // are a symmetric float pair: both carry the 0.5. halfDegree() is a
    // convenience for just the +0.5 bit.
    float temperatureC() const
    {
      return static_cast<float>(bytes[detail::kOffTemp] >> 1) +
             ((bytes[detail::kOffHalfDegree] & 0x80u) ? 0.5f : 0.0f);
    }
    bool halfDegree() const { return (bytes[detail::kOffHalfDegree] & 0x80u) != 0; }
    void setTemperatureC(float c)
    {
      int half_steps = static_cast<int>(c * 2.0f + 0.5f); // nearest 0.5C, e.g. 22.5 -> 45
      bytes[detail::kOffTemp] = static_cast<uint8_t>((half_steps / 2) << 1);
      bytes[detail::kOffHalfDegree] = static_cast<uint8_t>(
          (bytes[detail::kOffHalfDegree] & ~0x80u) | ((half_steps & 1) ? 0x80u : 0x00u));
    }

    // Quiet/powerful (byte21 flags) take precedence over the speed nibble: a real
    // remote forces the fan nibble to auto when either is engaged, and they are
    // mutually exclusive with a speed (one Fan choice). setFan() therefore writes
    // the nibble and (re)sets exactly one or neither of the two flags.
    Fan fan() const
    {
      uint8_t qp = bytes[detail::kOffQuietPowerful];
      if (qp & detail::kQuietBit) return Fan::QUIET;
      if (qp & detail::kPowerfulBit) return Fan::POWERFUL;
      return detail::nibbleToFan((bytes[detail::kOffFan] >> 4) & 0x0Fu);
    }
    void setFan(Fan f)
    {
      bytes[detail::kOffFan] = static_cast<uint8_t>((bytes[detail::kOffFan] & 0x0Fu) | (detail::fanToNibble(f) << 4));
      uint8_t qp = static_cast<uint8_t>(bytes[detail::kOffQuietPowerful] & ~(detail::kQuietBit | detail::kPowerfulBit));
      if (f == Fan::QUIET) qp |= detail::kQuietBit;
      else if (f == Fan::POWERFUL) qp |= detail::kPowerfulBit;
      bytes[detail::kOffQuietPowerful] = qp;
    }

    // Vertical louver position, in the low nibble of the fan byte (independent of
    // the fan speed in the high nibble).
    Louver louver() const { return static_cast<Louver>(bytes[detail::kOffFan] & 0x0Fu); }
    void setLouver(Louver v)
    {
      bytes[detail::kOffFan] = static_cast<uint8_t>((bytes[detail::kOffFan] & 0xF0u) | (static_cast<uint8_t>(v) & 0x0Fu));
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
      // Classify the model from the decoded marker bytes. The logical fields are
      // shared across models, so decoding is identical; only `model` differs.
      out.model = detail::detectModel(out.bytes);
      out.checksum_ok = (f2[detail::kFrame2Bytes - 1] == detail::checksum(f2));
      return true;
    }

    // state -> RAW ticks. Stamps the fixed signature/preamble and recomputes the
    // checksum, so a frame built from setters renders a valid burst.
    bool toRaw(esp32irpk::IRRawTickBuffer &out) const
    {
      // CKP is not implemented (toggle power + relocated quiet/powerful bits);
      // refuse rather than silently emitting a frame with the wrong semantics.
      if (model == Model::CKP)
        return false;

      uint8_t buf[kBytes];
      for (size_t i = 0; i < kBytes; ++i)
        buf[i] = bytes[i];
      for (size_t i = 0; i < detail::kFrame1Bytes; ++i)
        buf[i] = detail::kFrame1[i];
      for (size_t i = 0; i < sizeof(detail::kFrame2Preamble); ++i)
        buf[detail::kFrame1Bytes + i] = detail::kFrame2Preamble[i];
      detail::applyModelMarkers(buf, model); // stamp the model's marker bytes
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
