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
      bool level = true;
      uint16_t current_ticks = 0;
      for (int bit_index = 13; bit_index >= 0; --bit_index)
      {
        bool bit = ((bits >> bit_index) & 0x1ULL) != 0;
        if (!appendBiphaseBit(out, bit, 2, 89, level, current_ticks))
          return false;
      }
      return finishBiphase(out, current_ticks);
    }

    bool encodeRC6M0(uint64_t bits, IRRawTickBuffer &out)
    {
      if (!appendTicks(out, 266) || !appendTicks(out, 89))
        return false;
      bool level = true;
      uint16_t current_ticks = 0;

      if (!appendBiphaseBit(out, true, 4, 44, level, current_ticks))
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

      if (!appendBiphaseBit(out, true, 4, 44, level, current_ticks))
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
