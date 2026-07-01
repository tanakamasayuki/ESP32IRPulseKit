#pragma once

#include "../ESP32IRPulseKit.h"
#include "AcCodec.h"

// Kelvinator air-conditioner support (the standard 16-byte Kelvinator protocol,
// also used by some Gree/Sharp-badged remotes). A setting is two 8-byte command
// blocks. Each block is a header + 32 data bits + a 3-bit command footer (B010) +
// a ~20ms gap + 32 more data bits + a ~40ms gap. Bytes 8-10 repeat bytes 0-2, and
// each block carries a 4-bit block checksum in the high nibble of its last byte
// (byte 7 / byte 15). LSB-first. See SPEC 11.2.
//
// The frame does not fit the plain pulse-distance codec (the mid-block 3-bit
// marker and the two gap lengths), so toRaw/fromRaw render/scan it directly.
//
// This is a single-format vendor (no model axis). The logical field map is
// verified field-for-field against IRremoteESP8266's IRKelvinatorAC via the
// compat_matrix_ac studies. Swing (vertical position + horizontal), turbo, quiet,
// light, ion filter and X-Fan are documented but not settable here.

namespace esp32irpk::ac::Kelvinator
{

  // Per-vendor enums; values are the wire codes. Mode lives in byte 0 bits 0-2.
  enum class Mode : uint8_t
  {
    AUTO = 0,
    COOL = 1,
    DRY = 2,
    FAN = 3,
    HEAT = 4,
  };

  // Fan lives in byte 14 bits 4-6 (0-5). The encoder mirrors the low speeds into
  // the byte-0 BasicFan field (capped at 3), as a real remote does.
  enum class Fan : uint8_t
  {
    AUTO = 0,
    MIN_SPEED = 1,
    LOW_SPEED = 2,
    MED_SPEED = 3,
    HIGH_SPEED = 4,
    MAX_SPEED = 5,
  };

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
    // Standard Kelvinator timing (kKelvinatorTick = 85us). LSB-first.
    inline constexpr AcTiming kTiming = {
        /*header_mark_us=*/9010,
        /*header_space_us=*/4505,
        /*bit_mark_us=*/680,
        /*zero_space_us=*/510,
        /*one_space_us=*/1530,
        /*trailer_mark_us=*/680,
        /*frame_gap_us=*/39950, // 2 x the ~20ms inter-chunk gap
        /*tol_pct=*/40,         // wide decode window (see [[ac-decode-tolerance-loose]])
        /*lsb_first=*/true,
    };

    inline constexpr size_t kBytes = 16;
    inline constexpr size_t kBlockLen = 8;   // two 8-byte blocks
    inline constexpr size_t kChunkBits = 32; // each block is 32 + 32 data bits
    inline constexpr uint32_t kGapUs = 19975;      // ~20ms inter-chunk gap
    inline constexpr uint32_t kBlockGapUs = 39950; // ~40ms end-of-block gap
    inline constexpr uint8_t kFooterBits = 3;
    inline constexpr uint8_t kFooterValue = 0b010; // "B010" command footer

    // Fixed marker bytes (from IRKelvinatorAC::stateReset).
    inline constexpr uint8_t kMarker3 = 0x50;
    inline constexpr uint8_t kMarker11 = 0x70;

    inline constexpr size_t kOffMode = 0;   // bits 0-2 Mode, bit 3 Power, bits 4-5 BasicFan, bit 6 SwingAuto
    inline constexpr size_t kOffTemp = 1;   // bits 0-3 = degC - 16
    inline constexpr size_t kOffSwing = 4;  // bits 0-3 SwingV, bit 4 SwingH
    inline constexpr size_t kOffSum1 = 7;   // bits 4-7 block-1 checksum
    inline constexpr size_t kOffFan = 14;   // bits 4-6 Fan
    inline constexpr size_t kOffSum2 = 15;  // bits 4-7 block-2 checksum

    inline constexpr uint8_t kPowerMask = 0x08; // byte 0 bit 3
    inline constexpr uint8_t kMinTempC = 16;
    inline constexpr uint8_t kMaxTempC = 30;
    inline constexpr uint8_t kAutoTempC = 25;
    inline constexpr uint8_t kBasicFanMax = 3;
    inline constexpr uint8_t kFanMax = 5;
    inline constexpr uint8_t kChecksumStart = 10;

    inline bool isAutoOrDry(uint8_t mode) { return mode == 0 /*AUTO*/ || mode == 2 /*DRY*/; }

    // 4-bit block checksum (IRKelvinatorAC::calcBlockChecksum): sum the low nibble
    // of the block's first 4 bytes plus the high nibble of the next 3, +10, mod 16.
    inline uint8_t calcBlockChecksum(const uint8_t *block)
    {
      uint8_t sum = kChecksumStart;
      for (uint8_t i = 0; i < 4; ++i)
        sum += static_cast<uint8_t>(block[i] & 0x0Fu);
      for (uint8_t i = 4; i < 7; ++i)
        sum += static_cast<uint8_t>((block[i] >> 4) & 0x0Fu);
      return static_cast<uint8_t>(sum & 0x0Fu);
    }
    inline void writeChecksums(uint8_t *b)
    {
      b[kOffSum1] = static_cast<uint8_t>((b[kOffSum1] & 0x0Fu) | (calcBlockChecksum(b) << 4));
      b[kOffSum2] = static_cast<uint8_t>((b[kOffSum2] & 0x0Fu) | (calcBlockChecksum(b + kBlockLen) << 4));
    }
    inline bool checksumOk(const uint8_t *b)
    {
      return ((b[kOffSum1] >> 4) & 0x0Fu) == calcBlockChecksum(b) &&
             ((b[kOffSum2] >> 4) & 0x0Fu) == calcBlockChecksum(b + kBlockLen);
    }

    inline Mode codeToMode(uint8_t c)
    {
      switch (c & 0x7u)
      {
      case 1: return Mode::COOL;
      case 2: return Mode::DRY;
      case 3: return Mode::FAN;
      case 4: return Mode::HEAT;
      default: return Mode::AUTO;
      }
    }
    inline Fan codeToFan(uint8_t c)
    {
      switch (c & 0x7u)
      {
      case 1: return Fan::MIN_SPEED;
      case 2: return Fan::LOW_SPEED;
      case 3: return Fan::MED_SPEED;
      case 4: return Fan::HIGH_SPEED;
      case 5: return Fan::MAX_SPEED;
      default: return Fan::AUTO;
      }
    }
  } // namespace detail

  struct Frame
  {
    static constexpr size_t kBytes = detail::kBytes; // 16
    // 2 blocks x (header 2 + 32*2 bits + 3 footer bits + 2 chunk-gaps 2*2 + 2 block trailers)
    static constexpr size_t kMaxTicks = 2 * (2 + 64 + 3 + 3 + 2) * 2 + 16;

    // raw state: a known-good frame (on / cool / 24C / fan auto), fixed markers and
    // both block checksums precomputed. Bytes 8-10 repeat bytes 0-2.
    uint8_t bytes[kBytes] = {0x09, 0x08, 0x00, 0x50, 0x00, 0x00, 0x00, 0xB0,
                             0x09, 0x08, 0x00, 0x70, 0x00, 0x00, 0x00, 0xB0};
    uint16_t byte_length = 0;
    bool checksum_ok = false;

    bool power() const { return (bytes[detail::kOffMode] & detail::kPowerMask) != 0; }
    void setPower(bool on)
    {
      if (on)
        bytes[detail::kOffMode] |= detail::kPowerMask;
      else
        bytes[detail::kOffMode] &= static_cast<uint8_t>(~detail::kPowerMask);
    }

    Mode mode() const { return detail::codeToMode(bytes[detail::kOffMode]); }
    void setMode(Mode m)
    {
      const uint8_t code = static_cast<uint8_t>(m) & 0x7u;
      bytes[detail::kOffMode] = static_cast<uint8_t>((bytes[detail::kOffMode] & ~0x07u) | code);
      // Auto and Dry default to 25C on a real remote.
      if (detail::isAutoOrDry(code))
        setTemperatureC(detail::kAutoTempC);
    }

    uint8_t temperatureC() const
    {
      return static_cast<uint8_t>((bytes[detail::kOffTemp] & 0x0Fu) + detail::kMinTempC);
    }
    void setTemperatureC(uint8_t c)
    {
      if (c < detail::kMinTempC) c = detail::kMinTempC;
      if (c > detail::kMaxTempC) c = detail::kMaxTempC;
      bytes[detail::kOffTemp] = static_cast<uint8_t>(
          (bytes[detail::kOffTemp] & 0xF0u) | ((c - detail::kMinTempC) & 0x0Fu));
    }

    Fan fan() const { return detail::codeToFan((bytes[detail::kOffFan] >> 4) & 0x07u); }
    void setFan(Fan f)
    {
      const uint8_t speed = static_cast<uint8_t>(f) > detail::kFanMax
                                ? detail::kFanMax
                                : static_cast<uint8_t>(f);
      // Advanced fan (byte 14 bits 4-6).
      bytes[detail::kOffFan] = static_cast<uint8_t>(
          (bytes[detail::kOffFan] & ~(0x07u << 4)) | ((speed & 0x07u) << 4));
      // BasicFan (byte 0 bits 4-5), capped at 3, as IRKelvinatorAC does.
      const uint8_t basic = speed > detail::kBasicFanMax ? detail::kBasicFanMax : speed;
      bytes[detail::kOffMode] = static_cast<uint8_t>(
          (bytes[detail::kOffMode] & ~(0x03u << 4)) | ((basic & 0x03u) << 4));
    }

    bool swingHorizontal() const { return (bytes[detail::kOffSwing] >> 4) & 0x01u; }

    void printTo(Print &out) const
    {
      printAcSummary(out, "Kelvinator", power(), toString(mode()),
                     static_cast<float>(temperatureC()), toString(fan()), bytes,
                     byte_length, checksum_ok);
      out.print("// swingH=");
      out.println(swingHorizontal() ? "on" : "off");
    }

    void printSetterSnippet(Print &out) const
    {
      out.println("// send code (Kelvinator AC, editable -- lossy: swing/turbo/quiet use defaults):");
      out.println("esp32irpk::ac::Kelvinator::Frame f;");
      out.print("f.setPower(");
      out.print(power() ? "true" : "false");
      out.println(");");
      out.print("f.setMode(esp32irpk::ac::Kelvinator::Mode::");
      out.print(toString(mode()));
      out.println(");");
      out.print("f.setTemperatureC(");
      out.print((unsigned)temperatureC());
      out.println(");");
      out.print("f.setFan(esp32irpk::ac::Kelvinator::Fan::");
      out.print(toString(fan()));
      out.println(");");
      out.println("esp32irpk::ac::send(tx, f);");
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

    // RAW ticks -> state bytes. false unless the two-block Kelvinator structure
    // (header, 32 bits, B010 footer, gap, 32 bits, block gap; x2) decodes and the
    // fixed marker bytes match; checksum validity is reported via `checksum_ok`.
    static bool fromRaw(const esp32irpk::IRRawTickView &raw, Frame &out)
    {
      out = Frame{};
      if (!raw.ticks)
        return false;
      const AcTiming &t = detail::kTiming;
      namespace d = esp32irpk::ac::detail;
      const uint32_t one_threshold_us = (t.zero_space_us + t.one_space_us) / 2;

      // Read `nbits` pulse-distance bits (LSB-first) into `b` starting at bit index
      // `bit0`; advance `pos`. Each bit is a bit-mark + a 0/1-length space.
      auto readBits = [&](size_t &pos, size_t nbits, uint8_t *b, size_t bit0) -> bool {
        for (size_t i = 0; i < nbits; ++i)
        {
          if (pos + 1 >= raw.len)
            return false;
          if (!d::within(static_cast<uint32_t>(raw.ticks[pos]) * d::kTickUs, t.bit_mark_us, t.tol_pct))
            return false;
          const uint32_t space_us = static_cast<uint32_t>(raw.ticks[pos + 1]) * d::kTickUs;
          if (space_us >= one_threshold_us)
            b[(bit0 + i) / 8] |= static_cast<uint8_t>(1u << ((bit0 + i) % 8));
          pos += 2;
        }
        return true;
      };
      // Consume a bit-mark + a following (long) gap; only the mark is range-checked.
      // The gap after the very last block's trailer mark is absent when this is the
      // final frame of a capture (the RX does not record the trailing idle space),
      // so accept a trailer mark with no gap after it.
      auto consumeMarkGap = [&](size_t &pos) -> bool {
        if (pos >= raw.len)
          return false;
        if (!d::within(static_cast<uint32_t>(raw.ticks[pos]) * d::kTickUs, t.bit_mark_us, t.tol_pct))
          return false;
        pos += (pos + 1 < raw.len) ? 2 : 1;
        return true;
      };

      for (size_t start = 0; start + 1 < raw.len; ++start)
      {
        if (!d::within(static_cast<uint32_t>(raw.ticks[start]) * d::kTickUs, t.header_mark_us, t.tol_pct) ||
            !d::within(static_cast<uint32_t>(raw.ticks[start + 1]) * d::kTickUs, t.header_space_us, t.tol_pct))
          continue;

        uint8_t b[detail::kBytes] = {};
        size_t pos = start;
        bool ok = true;
        for (uint8_t blk = 0; blk < 2 && ok; ++blk)
        {
          const size_t base = blk * detail::kBlockLen;
          // Header (skip the second block's header only if present -- both have one).
          if (pos + 1 >= raw.len ||
              !d::within(static_cast<uint32_t>(raw.ticks[pos]) * d::kTickUs, t.header_mark_us, t.tol_pct) ||
              !d::within(static_cast<uint32_t>(raw.ticks[pos + 1]) * d::kTickUs, t.header_space_us, t.tol_pct))
          { ok = false; break; }
          pos += 2;
          // 32 data bits (bytes base..base+3).
          if (!readBits(pos, detail::kChunkBits, b, base * 8)) { ok = false; break; }
          // 3-bit command footer, must be B010.
          uint8_t footer = 0;
          if (!readBits(pos, detail::kFooterBits, &footer, 0)) { ok = false; break; }
          if ((footer & 0x07u) != detail::kFooterValue) { ok = false; break; }
          // Footer trailer mark + ~20ms gap.
          if (!consumeMarkGap(pos)) { ok = false; break; }
          // 32 more data bits (bytes base+4..base+7).
          if (!readBits(pos, detail::kChunkBits, b, (base + 4) * 8)) { ok = false; break; }
          // Block trailer mark + ~40ms gap.
          if (!consumeMarkGap(pos)) { ok = false; break; }
        }
        if (!ok)
          continue;
        // Fixed markers and the block-0/block-1 repeat must hold.
        if (b[3] != detail::kMarker3 || b[11] != detail::kMarker11 ||
            b[8] != b[0] || b[9] != b[1] || b[10] != b[2])
          continue;

        for (size_t i = 0; i < detail::kBytes; ++i)
          out.bytes[i] = b[i];
        out.byte_length = kBytes;
        out.checksum_ok = detail::checksumOk(out.bytes);
        return true;
      }
      return false;
    }

    // state -> RAW ticks. Forces the fixed markers, mirrors bytes 0-2 into 8-10,
    // recomputes both block checksums, then renders the two blocks (header, 32
    // bits, B010 footer, ~20ms gap, 32 bits, ~40ms gap).
    bool toRaw(esp32irpk::IRRawTickBuffer &out) const
    {
      uint8_t buf[kBytes];
      for (size_t i = 0; i < kBytes; ++i)
        buf[i] = bytes[i];
      buf[3] = detail::kMarker3;
      buf[11] = detail::kMarker11;
      buf[8] = buf[0];
      buf[9] = buf[1];
      buf[10] = buf[2];
      detail::writeChecksums(buf);

      const AcTiming &t = detail::kTiming;
      namespace d = esp32irpk::ac::detail;
      out.len = 0;
      auto push = [&](uint16_t tick) -> bool {
        if (out.len >= out.capacity)
          return false;
        out.ticks[out.len++] = tick;
        return true;
      };
      auto emitBits = [&](const uint8_t *b, size_t nbits) -> bool {
        for (size_t i = 0; i < nbits; ++i)
        {
          const bool one = (b[i / 8] >> (i % 8)) & 0x1u; // LSB-first
          if (!push(d::usToTicks(t.bit_mark_us)) ||
              !push(d::usToTicks(one ? t.one_space_us : t.zero_space_us)))
            return false;
        }
        return true;
      };

      for (uint8_t blk = 0; blk < 2; ++blk)
      {
        const uint8_t *base = buf + blk * detail::kBlockLen;
        if (!push(d::usToTicks(t.header_mark_us)) || !push(d::usToTicks(t.header_space_us)))
          return false;
        if (!emitBits(base, detail::kChunkBits)) // bytes 0-3 of the block
          return false;
        // 3-bit command footer (B010), LSB-first.
        for (uint8_t i = 0; i < detail::kFooterBits; ++i)
        {
          const bool one = (detail::kFooterValue >> i) & 0x1u;
          if (!push(d::usToTicks(t.bit_mark_us)) ||
              !push(d::usToTicks(one ? t.one_space_us : t.zero_space_us)))
            return false;
        }
        // Footer trailer mark + ~20ms gap.
        if (!push(d::usToTicks(t.bit_mark_us)) || !push(d::usToTicks(detail::kGapUs)))
          return false;
        if (!emitBits(base + 4, detail::kChunkBits)) // bytes 4-7 of the block
          return false;
        // Block trailer mark + ~40ms gap.
        if (!push(d::usToTicks(t.bit_mark_us)) || !push(d::usToTicks(detail::kBlockGapUs)))
          return false;
      }
      return true;
    }
  };

} // namespace esp32irpk::ac::Kelvinator
