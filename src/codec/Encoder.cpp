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

    bool isValidBitLength(const IRProtocolSpec &spec, uint16_t bit_length)
    {
      uint16_t min_bits = spec.min_bit_length ? spec.min_bit_length : spec.bit_length;
      uint16_t max_bits = spec.max_bit_length ? spec.max_bit_length : spec.bit_length;
      if (min_bits == 0 || max_bits == 0 || min_bits > max_bits)
        return false;
      return bit_length >= min_bits && bit_length <= max_bits;
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
