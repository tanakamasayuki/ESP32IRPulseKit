#include "Encoder.h"

namespace esp32irpk::codec
{

  namespace
  {
    constexpr uint32_t kTickUs = 10;

    uint16_t usToTicks(uint32_t us)
    {
      return static_cast<uint16_t>((us + (kTickUs / 2)) / kTickUs);
    }

    const IRProtocolSpec *findSpec(IRProtocolID id, const IRProtocolSpec *specs, size_t spec_count)
    {
      if (!specs || spec_count == 0)
        return nullptr;
      for (size_t i = 0; i < spec_count; ++i)
      {
        if (specs[i].protocol_id == id)
          return &specs[i];
      }
      return nullptr;
    }

    bool appendPulse(IRRawTickBuffer &out, uint32_t us)
    {
      if (us == 0)
        return true;
      if (!out.ticks || out.capacity == 0)
        return false;
      if (out.len >= out.capacity)
        return false;
      out.ticks[out.len++] = usToTicks(us);
      return true;
    }

    bool appendTicks(IRRawTickBuffer &out, uint16_t ticks)
    {
      if (ticks == 0)
        return true;
      if (!out.ticks || out.capacity == 0)
        return false;
      if (out.len >= out.capacity)
        return false;
      out.ticks[out.len++] = ticks;
      return true;
    }

    bool isValidBitLength(const IRProtocolSpec &spec, uint16_t bit_length)
    {
      uint16_t min_bits = spec.min_bit_length ? spec.min_bit_length : spec.bit_length;
      uint16_t max_bits = spec.max_bit_length ? spec.max_bit_length : spec.bit_length;
      if (min_bits == 0 || max_bits == 0 || min_bits > max_bits)
        return false;
      return bit_length >= min_bits && bit_length <= max_bits;
    }

    bool appendHalf(IRRawTickBuffer &out,
                    bool half_level,
                    uint16_t unit_ticks,
                    bool &level,
                    uint16_t &current_ticks)
    {
      if (half_level == level)
      {
        current_ticks = static_cast<uint16_t>(current_ticks + unit_ticks);
        return true;
      }
      if (current_ticks > 0 && !appendTicks(out, current_ticks))
        return false;
      current_ticks = unit_ticks;
      level = half_level;
      return true;
    }

    bool appendBiphaseBit(IRRawTickBuffer &out,
                          bool bit,
                          uint8_t width_halves,
                          uint16_t unit_ticks,
                          bool &level,
                          uint16_t &current_ticks)
    {
      if (width_halves < 2 || (width_halves % 2) != 0)
        return false;
      uint8_t half_count = width_halves / 2;
      for (uint8_t i = 0; i < half_count; ++i)
      {
        if (!appendHalf(out, bit, unit_ticks, level, current_ticks))
          return false;
      }
      for (uint8_t i = 0; i < half_count; ++i)
      {
        if (!appendHalf(out, !bit, unit_ticks, level, current_ticks))
          return false;
      }
      return true;
    }

    bool finishBiphase(IRRawTickBuffer &out, uint16_t current_ticks)
    {
      return current_ticks == 0 || appendTicks(out, current_ticks);
    }

    bool encodeRC5(uint64_t bits, IRRawTickBuffer &out)
    {
      // Standard RC5: MSB-first, 14 bits, '1' = space then mark, '0' = mark then
      // space. The first half-bit is a space when the leading start bit is '1'
      // (RC5 S1 is always '1'); that space is the idle gap and is not part of the
      // captured RAW, so emission begins at the first mark.
      constexpr uint16_t kUnitTicks = 89; // ~890 us half-bit
      bool halves[28];
      for (int i = 0; i < 14; ++i)
      {
        bool bit = ((bits >> (13 - i)) & 0x1ULL) != 0;
        halves[2 * i] = !bit;    // first half: space for '1', mark for '0'
        halves[2 * i + 1] = bit; // second half: mark for '1', space for '0'
      }
      size_t i = 0;
      while (i < 28 && !halves[i]) // drop leading idle space half-bit(s)
        ++i;
      bool level = true; // RAW begins with a mark
      uint16_t run_ticks = 0;
      for (; i < 28; ++i)
      {
        if (halves[i] == level)
        {
          run_ticks = static_cast<uint16_t>(run_ticks + kUnitTicks);
        }
        else
        {
          if (!appendTicks(out, run_ticks))
            return false;
          run_ticks = kUnitTicks;
          level = halves[i];
        }
      }
      return finishBiphase(out, run_ticks);
    }

    bool encodeRC6M0(uint64_t bits, IRRawTickBuffer &out)
    {
      if (!appendTicks(out, 266) || !appendTicks(out, 89))
        return false;
      bool level = true;
      uint16_t current_ticks = 0;

      // Start bit: single-width '1' (mark then space). Only the toggle bit below
      // is double-width.
      if (!appendBiphaseBit(out, true, 2, 44, level, current_ticks))
        return false;
      for (int bit_index = 19; bit_index >= 17; --bit_index)
      {
        bool bit = ((bits >> bit_index) & 0x1ULL) != 0;
        if (!appendBiphaseBit(out, bit, 2, 44, level, current_ticks))
          return false;
      }
      bool toggle = ((bits >> 16) & 0x1ULL) != 0;
      if (!appendBiphaseBit(out, toggle, 4, 44, level, current_ticks))
        return false;
      for (int bit_index = 15; bit_index >= 0; --bit_index)
      {
        bool bit = ((bits >> bit_index) & 0x1ULL) != 0;
        if (!appendBiphaseBit(out, bit, 2, 44, level, current_ticks))
          return false;
      }
      return finishBiphase(out, current_ticks);
    }

    bool encodeRC6M6(uint64_t bits, IRRawTickBuffer &out)
    {
      if (!appendTicks(out, 266) || !appendTicks(out, 89))
        return false;
      bool level = true;
      uint16_t current_ticks = 0;

      // Start bit: single-width '1' (mark then space).
      if (!appendBiphaseBit(out, true, 2, 44, level, current_ticks))
        return false;
      for (int bit_index = 34; bit_index >= 32; --bit_index)
      {
        bool bit = ((bits >> bit_index) & 0x1ULL) != 0;
        if (!appendBiphaseBit(out, bit, 2, 44, level, current_ticks))
          return false;
      }
      for (int bit_index = 31; bit_index >= 0; --bit_index)
      {
        bool bit = ((bits >> bit_index) & 0x1ULL) != 0;
        if (!appendBiphaseBit(out, bit, 2, 44, level, current_ticks))
          return false;
      }
      return finishBiphase(out, current_ticks);
    }

    // SAMSUNG36: two blocks (16 + 20 bits) sent MSB-first, separated by a
    // header-length space (with each block ending on a footer mark). The 36-bit
    // value is MSB-first: bits[35..20] = block1, bits[19..0] = block2. See
    // src/protocols/Samsung.h.
    bool encodeSamsung36(uint64_t bits, const IRProtocolSpec &spec, IRRawTickBuffer &out)
    {
      constexpr uint16_t kBlock1Bits = 16;
      constexpr uint16_t kBlock2Bits = 20;

      // Header.
      if (!appendPulse(out, spec.header.mark_us) || !appendPulse(out, spec.header.space_us))
        return false;

      // Block 1: top 16 bits (positions 35..20), MSB-first.
      for (uint16_t k = 0; k < kBlock1Bits; ++k)
      {
        int bit_index = 35 - k;
        bool bit = ((bits >> bit_index) & 0x1ULL) != 0;
        const IRPulseUs &pulse = bit ? spec.one : spec.zero;
        if (!appendPulse(out, pulse.mark_us) || !appendPulse(out, pulse.space_us))
          return false;
      }

      // Block 1 footer mark + inter-block separator space (= header space).
      if (!appendPulse(out, spec.trailer.mark_us) || !appendPulse(out, spec.header.space_us))
        return false;

      // Block 2: low 20 bits (positions 19..0), MSB-first.
      for (uint16_t k = 0; k < kBlock2Bits; ++k)
      {
        int bit_index = 19 - k;
        bool bit = ((bits >> bit_index) & 0x1ULL) != 0;
        const IRPulseUs &pulse = bit ? spec.one : spec.zero;
        if (!appendPulse(out, pulse.mark_us) || !appendPulse(out, pulse.space_us))
          return false;
      }

      // Trailer mark (frame end; the RX detects the gap via idle threshold).
      if (!appendPulse(out, spec.trailer.mark_us) || !appendPulse(out, spec.trailer.space_us))
        return false;

      return true;
    }
  } // namespace

  bool encodeBitsToRaw(const IRDecodedBits &decoded,
                       const IRProtocolSpec *specs,
                       size_t spec_count,
                       IRRawTickBuffer &out_raw)
  {
    out_raw.len = 0;
    if (!out_raw.ticks || out_raw.capacity == 0)
      return false;

    const IRProtocolSpec *spec = findSpec(decoded.protocol_id, specs, spec_count);
    if (!spec)
      return false;

    if (decoded.frame_type == IRFrameType::REPEAT)
    {
      if (!spec->has_repeat)
        return false;
      if (!appendPulse(out_raw, spec->repeat_header.mark_us) ||
          !appendPulse(out_raw, spec->repeat_header.space_us) ||
          !appendPulse(out_raw, spec->trailer.mark_us) ||
          !appendPulse(out_raw, spec->trailer.space_us))
      {
        out_raw.len = 0;
        return false;
      }
      return true;
    }

    if (decoded.bit_length == 0 || decoded.bit_length > 64 || !isValidBitLength(*spec, decoded.bit_length))
      return false;

    if (spec->scheme == IRProtocolScheme::BIPHASE)
    {
      bool ok = false;
      switch (spec->protocol_id)
      {
      case IRProtocolID::RC5:
        ok = decoded.bit_length == 14 && encodeRC5(decoded.bits, out_raw);
        break;
      case IRProtocolID::RC6_M0_16:
        ok = decoded.bit_length == 21 && encodeRC6M0(decoded.bits, out_raw);
        break;
      case IRProtocolID::RC6_M6_32:
        ok = decoded.bit_length == 36 && encodeRC6M6(decoded.bits, out_raw);
        break;
      default:
        ok = false;
        break;
      }
      if (!ok)
        out_raw.len = 0;
      return ok;
    }

    if (spec->scheme != IRProtocolScheme::SPACE_ENC)
      return false;

    // SAMSUNG36 has a two-block waveform that the generic SPACE_ENC loop below
    // cannot express, so it uses a protocol-specific encoder.
    if (spec->protocol_id == IRProtocolID::SAMSUNG36)
    {
      bool ok = decoded.bit_length == 36 && encodeSamsung36(decoded.bits, *spec, out_raw);
      if (!ok)
        out_raw.len = 0;
      return ok;
    }

    if (!appendPulse(out_raw, spec->header.mark_us) ||
        !appendPulse(out_raw, spec->header.space_us))
    {
      out_raw.len = 0;
      return false;
    }

    for (uint16_t i = 0; i < decoded.bit_length; ++i)
    {
      uint16_t bit_index = spec->lsb_first ? i : static_cast<uint16_t>(decoded.bit_length - 1 - i);
      bool bit = ((decoded.bits >> bit_index) & 0x1ULL) != 0;
      const IRPulseUs &pulse = bit ? spec->one : spec->zero;
      if (!appendPulse(out_raw, pulse.mark_us) || !appendPulse(out_raw, pulse.space_us))
      {
        out_raw.len = 0;
        return false;
      }
    }

    if (!appendPulse(out_raw, spec->trailer.mark_us) ||
        !appendPulse(out_raw, spec->trailer.space_us))
    {
      out_raw.len = 0;
      return false;
    }

    return true;
  }

} // namespace esp32irpk::codec
