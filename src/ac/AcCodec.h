#pragma once

#include "../ESP32IRPulseKit.h"

// Generic pulse-distance (NEC/AEHA-like) tick<->byte conversion shared by AC
// vendors. AC frames are byte-structured and longer than the 64-bit generic
// codec, so this layer works on raw byte arrays instead of IRDecodedBits.

namespace esp32irpk::ac
{

  namespace detail
  {
    constexpr uint32_t kTickUs = 10; // RAW tick unit

    inline uint16_t usToTicks(uint32_t us)
    {
      return static_cast<uint16_t>((us + kTickUs / 2) / kTickUs);
    }

    inline bool within(uint32_t actual_us, uint32_t nominal_us, uint8_t tol_pct)
    {
      const uint32_t tol = nominal_us * tol_pct / 100;
      return actual_us + tol >= nominal_us && actual_us <= nominal_us + tol;
    }
  } // namespace detail

  // Pulse-distance timing for one vendor frame: a leading header, each bit a
  // fixed mark plus a 0/1-length space, a trailing mark, and a long gap between
  // concatenated frames.
  struct AcTiming
  {
    uint16_t header_mark_us = 0;
    uint16_t header_space_us = 0;
    uint16_t bit_mark_us = 0;
    uint16_t zero_space_us = 0;
    uint16_t one_space_us = 0;
    uint16_t trailer_mark_us = 0;
    uint16_t frame_gap_us = 0; // gap separating concatenated frames
    uint8_t tol_pct = 30;      // matching tolerance
    bool lsb_first = true;     // bit order within each byte
  };

  // Decode one pulse-distance frame from `raw` starting at tick index `pos`.
  // On success returns the decoded bit count and advances `pos` past the frame;
  // returns 0 on mismatch. `with_header` selects whether a leading header pair
  // is expected: some vendors (e.g. Gree) concatenate a second block with no
  // header of its own.
  inline size_t rawFrameToBytes(const esp32irpk::IRRawTickView &raw, size_t &pos,
                                const AcTiming &t, uint8_t *out, size_t out_cap,
                                bool with_header = true)
  {
    using namespace detail;
    if (!raw.ticks || out_cap == 0)
      return 0;
    if (with_header)
    {
      // Need the header pair.
      if (pos + 2 > raw.len)
        return 0;
      if (!within(raw.ticks[pos] * kTickUs, t.header_mark_us, t.tol_pct) ||
          !within(raw.ticks[pos + 1] * kTickUs, t.header_space_us, t.tol_pct))
        return 0;
      pos += 2;
    }

    for (size_t i = 0; i < out_cap; ++i)
      out[i] = 0;

    // Classify each bit's space by nearest of the 0/1 lengths rather than by
    // narrow windows around each: real IR receivers (and senders) skew mark and
    // space lengths by ~100us, so a fixed-window scheme leaves a dead zone that
    // rejects otherwise-valid frames. The classification is order-agnostic (some
    // vendors, e.g. Mitsubishi Heavy, use a SHORTER space for 1 than for 0), so
    // it compares distance to each nominal length instead of a directional
    // threshold. A space far longer than the longest data space is the trailer's
    // inter-frame gap, which ends the frame. Integrity is enforced by the vendor
    // checksum, not by tight per-bit windows.
    const uint32_t max_data_space_us =
        (t.one_space_us > t.zero_space_us) ? t.one_space_us : t.zero_space_us;
    const uint32_t frame_end_us =
        (t.frame_gap_us > max_data_space_us)
            ? (max_data_space_us + t.frame_gap_us) / 2
            : max_data_space_us + max_data_space_us / 2;

    const size_t max_bits = out_cap * 8;
    size_t bit_count = 0;
    while (pos + 1 < raw.len)
    {
      const uint32_t mark_us = raw.ticks[pos] * kTickUs;
      const uint32_t space_us = raw.ticks[pos + 1] * kTickUs;
      if (!within(mark_us, t.bit_mark_us, t.tol_pct))
        return 0; // broken bit mark
      if (space_us >= frame_end_us)
      {
        // This mark is the trailer and the space is the inter-frame gap.
        pos += 2;
        return bit_count;
      }
      if (bit_count >= max_bits)
        return 0; // payload exceeds caller buffer
      const uint32_t d_one = (space_us > t.one_space_us) ? space_us - t.one_space_us
                                                         : t.one_space_us - space_us;
      const uint32_t d_zero = (space_us > t.zero_space_us) ? space_us - t.zero_space_us
                                                           : t.zero_space_us - space_us;
      if (d_one <= d_zero)
      {
        // Bit order within the byte follows t.lsb_first (MSB-first vendors, e.g.
        // Toshiba, send bit 7 first).
        const size_t bit_in_byte = t.lsb_first ? (bit_count % 8) : (7 - (bit_count % 8));
        out[bit_count / 8] |= static_cast<uint8_t>(1u << bit_in_byte);
      }
      // else bit 0: leave the cleared bit as-is
      ++bit_count;
      pos += 2;
    }
    // Ran out of ticks: a trailing trailer mark with no recorded gap (last
    // frame of the capture) still ends the frame.
    if (pos < raw.len)
      pos = raw.len;
    return bit_count;
  }

  // Encode `bit_len` bits from `bytes` as one pulse-distance frame appended to
  // `out`; bit order within each byte follows `t.lsb_first`. Returns false on
  // capacity overflow.
  // `with_header` selects whether a leading header pair is emitted: some vendors
  // (e.g. Gree) concatenate a second block with no header of its own.
  inline bool bytesFrameToRaw(const uint8_t *bytes, size_t bit_len,
                              const AcTiming &t, esp32irpk::IRRawTickBuffer &out,
                              bool with_header = true)
  {
    using namespace detail;
    if (!out.ticks)
      return false;
    auto push = [&](uint16_t tick) -> bool
    {
      if (out.len >= out.capacity)
        return false;
      out.ticks[out.len++] = tick;
      return true;
    };

    if (with_header)
      if (!push(usToTicks(t.header_mark_us)) || !push(usToTicks(t.header_space_us)))
        return false;
    for (size_t i = 0; i < bit_len; ++i)
    {
      const size_t bit_in_byte = t.lsb_first ? (i % 8) : (7 - (i % 8));
      const bool one = (bytes[i / 8] >> bit_in_byte) & 0x1u;
      if (!push(usToTicks(t.bit_mark_us)))
        return false;
      if (!push(usToTicks(one ? t.one_space_us : t.zero_space_us)))
        return false;
    }
    if (!push(usToTicks(t.trailer_mark_us)) || !push(usToTicks(t.frame_gap_us)))
      return false;
    return true;
  }

  // Debug formatting shared by the vendor Frames' printTo(): emits the common
  // "power/mode/temp/fan/checksum" line plus the full decoded state in hex. The
  // vendor Frame calls this, then appends its own fields. It takes Arduino's
  // Print (Serial is one) rather than hardcoding an output, so a caller can dump
  // to any stream and it still compiles on the host test core. `mode` and `fan`
  // are passed as names (via each vendor's toString); the caller maps them.
  inline void printAcSummary(Print &out, const char *vendor, bool power,
                             const char *mode, float temp_c, const char *fan,
                             const uint8_t *bytes, size_t len, bool checksum_ok)
  {
    out.print("// decoded: ");
    out.print(vendor);
    out.print(" AC  power=");
    out.print(power ? "on" : "off");
    out.print(" mode=");
    out.print(mode);
    out.print(" temp=");
    out.print(temp_c, 1); // one decimal: shows 0.5C steps where supported
    out.print("C fan=");
    out.print(fan);
    out.print("  checksum=");
    out.println(checksum_ok ? "ok" : "BAD");
    out.print("// bytes:");
    for (size_t i = 0; i < len; ++i)
    {
      out.print(' ');
      if (bytes[i] < 0x10)
        out.print('0');
      out.print(bytes[i], HEX);
    }
    out.println();
  }

  // Copy-paste C++ that rebuilds an AC frame from its decoded state bytes and
  // sends it — the compact, bit-exact alternative to dumping the (much longer)
  // RAW tick array. `vendor` is the short label for the comment; `typeName` is
  // the fully-qualified Frame type, e.g. "esp32irpk::ac::Panasonic::Frame".
  inline void printAcStateSnippet(Print &out, const char *vendor, const char *typeName,
                                  const uint8_t *bytes, size_t len)
  {
    out.print("// send code (");
    out.print(vendor);
    out.println(" AC state, bit-exact replay):");
    out.print(typeName);
    out.println(" f;");
    out.print("static const uint8_t state[] = {");
    for (size_t i = 0; i < len; ++i)
    {
      out.print(i ? "," : "");
      out.print((i % 12 == 0) ? "\n  " : " ");
      out.print("0x");
      if (bytes[i] < 0x10)
        out.print('0');
      out.print(bytes[i], HEX);
    }
    out.println(" };");
    out.print(typeName);
    out.println("::fromBytes(state, sizeof(state), f);");
    out.println("esp32irpk::ac::send(tx, f);");
  }

} // namespace esp32irpk::ac
