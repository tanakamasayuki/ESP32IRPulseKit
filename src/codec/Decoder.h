#pragma once

#include "../ESP32IRPulseKit.h"
#include <cstring>

namespace esp32irpk::codec
{

  namespace detail
  {
    constexpr uint32_t kTickUs = 10;

    inline uint32_t ticksToUs(uint16_t ticks)
    {
      return static_cast<uint32_t>(ticks) * kTickUs;
    }

    inline uint32_t absDiff(uint32_t a, uint32_t b)
    {
      return (a > b) ? (a - b) : (b - a);
    }

    inline uint16_t errorPct(uint32_t actual_us, uint32_t expected_us)
    {
      if (expected_us == 0)
        return actual_us == 0 ? 0 : 100;
      uint32_t diff = absDiff(actual_us, expected_us);
      return static_cast<uint16_t>((diff * 100U + (expected_us / 2U)) / expected_us);
    }

    inline bool withinTol(uint32_t actual_us, uint32_t expected_us, uint16_t tol_pct)
    {
      if (expected_us == 0)
        return actual_us == 0;
      uint32_t diff = absDiff(actual_us, expected_us);
      return (diff * 100U) <= (expected_us * tol_pct);
    }

    struct PulseMatch
    {
      bool ok = false;
      uint16_t error_pct = 0;
    };

    inline PulseMatch matchPulse(uint32_t actual_us, uint32_t expected_us, uint16_t tol_pct)
    {
      PulseMatch out{};
      out.error_pct = errorPct(actual_us, expected_us);
      out.ok = withinTol(actual_us, expected_us, tol_pct);
      return out;
    }

    inline bool consumePulse(const IRRawTickView &raw,
                             size_t &idx,
                             uint32_t expected_us,
                             uint16_t tol_pct,
                             uint32_t &error_sum)
    {
      if (expected_us == 0)
        return true;
      if (idx >= raw.len)
        return false;
      uint32_t actual_us = ticksToUs(raw.ticks[idx]);
      PulseMatch m = matchPulse(actual_us, expected_us, tol_pct);
      if (!m.ok)
        return false;
      error_sum += m.error_pct;
      ++idx;
      return true;
    }

    inline int16_t finalizeScore(uint32_t error_sum)
    {
      int32_t score = 1000 - static_cast<int32_t>(error_sum);
      if (score > 32767)
        score = 32767;
      if (score < -32768)
        score = -32768;
      return static_cast<int16_t>(score);
    }

    inline bool decodeNormal(const IRRawTickView &raw,
                             const IRProtocolSpec &spec,
                             IRDecodedBits &decoded,
                             int16_t &score_out)
    {
      if (spec.bit_length == 0)
        return false;
      size_t idx = 0;
      uint32_t error_sum = 0;

      if (!consumePulse(raw, idx, spec.header.mark_us, spec.bit_tol_pct, error_sum) ||
          !consumePulse(raw, idx, spec.header.space_us, spec.bit_tol_pct, error_sum))
        return false;

      uint64_t bits = 0;
      for (uint16_t i = 0; i < spec.bit_length; ++i)
      {
        if (idx + 1 >= raw.len)
          return false;

        uint32_t mark_us = ticksToUs(raw.ticks[idx]);
        uint32_t space_us = ticksToUs(raw.ticks[idx + 1]);

        PulseMatch one_mark = matchPulse(mark_us, spec.one.mark_us, spec.bit_tol_pct);
        PulseMatch one_space = matchPulse(space_us, spec.one.space_us, spec.bit_tol_pct);
        PulseMatch zero_mark = matchPulse(mark_us, spec.zero.mark_us, spec.bit_tol_pct);
        PulseMatch zero_space = matchPulse(space_us, spec.zero.space_us, spec.bit_tol_pct);

        bool one_ok = one_mark.ok && one_space.ok;
        bool zero_ok = zero_mark.ok && zero_space.ok;
        if (!one_ok && !zero_ok)
          return false;

        uint32_t one_err = static_cast<uint32_t>(one_mark.error_pct + one_space.error_pct);
        uint32_t zero_err = static_cast<uint32_t>(zero_mark.error_pct + zero_space.error_pct);
        bool bit_is_one = false;
        uint32_t bit_err = 0;

        if (one_ok && (!zero_ok || one_err <= zero_err))
        {
          bit_is_one = true;
          bit_err = one_err;
        }
        else
        {
          bit_is_one = false;
          bit_err = zero_err;
        }

        if (spec.lsb_first)
        {
          if (bit_is_one)
            bits |= (1ULL << i);
        }
        else
        {
          if (bit_is_one)
            bits |= (1ULL << (spec.bit_length - 1 - i));
        }

        error_sum += bit_err;
        idx += 2;
      }

      if (!consumePulse(raw, idx, spec.trailer.mark_us, spec.bit_tol_pct, error_sum) ||
          !consumePulse(raw, idx, spec.trailer.space_us, spec.bit_tol_pct, error_sum))
        return false;

      if (idx < raw.len)
      {
        if (raw.len - idx == 1)
        {
          if (spec.frame_end_gap_us > 0)
          {
            uint32_t gap_us = ticksToUs(raw.ticks[idx]);
            error_sum += errorPct(gap_us, spec.frame_end_gap_us);
          }
          ++idx;
        }
        else
        {
          return false;
        }
      }

      decoded.protocol_id = spec.protocol_id;
      decoded.frame_type = IRFrameType::NORMAL;
      decoded.bit_length = spec.bit_length;
      decoded.bits = bits;
      score_out = finalizeScore(error_sum);
      return true;
    }

    inline bool decodeRepeat(const IRRawTickView &raw,
                             const IRProtocolSpec &spec,
                             IRDecodedBits &decoded,
                             int16_t &score_out)
    {
      if (!spec.has_repeat)
        return false;
      size_t idx = 0;
      uint32_t error_sum = 0;

      if (!consumePulse(raw, idx, spec.repeat_header.mark_us, spec.bit_tol_pct, error_sum) ||
          !consumePulse(raw, idx, spec.repeat_header.space_us, spec.bit_tol_pct, error_sum))
        return false;

      if (!consumePulse(raw, idx, spec.trailer.mark_us, spec.bit_tol_pct, error_sum) ||
          !consumePulse(raw, idx, spec.trailer.space_us, spec.bit_tol_pct, error_sum))
        return false;

      if (idx < raw.len)
      {
        if (raw.len - idx == 1)
        {
          uint32_t gap_us = ticksToUs(raw.ticks[idx]);
          uint32_t expected_gap = spec.repeat_gap_us > 0 ? spec.repeat_gap_us : spec.frame_end_gap_us;
          if (expected_gap > 0)
            error_sum += errorPct(gap_us, expected_gap);
          ++idx;
        }
        else
        {
          return false;
        }
      }

      decoded.protocol_id = spec.protocol_id;
      decoded.frame_type = IRFrameType::REPEAT;
      decoded.bit_length = 0;
      decoded.bits = 0xFFFFFFFFFFFFFFFFULL;
      score_out = finalizeScore(error_sum);
      return true;
    }

    template <size_t MaxCandidates>
    inline void insertCandidate(IRReceiveResult<MaxCandidates> &out,
                                uint8_t max_candidates,
                                const IRDecodeCandidate &cand)
    {
      if (max_candidates == 0)
        return;
      uint8_t limit = max_candidates > MaxCandidates ? MaxCandidates : max_candidates;
      if (limit == 0)
        return;

      if (out.count < limit)
      {
        out.candidates[out.count] = cand;
        ++out.count;
      }
      else
      {
        if (cand.score <= out.candidates[limit - 1].score)
          return;
        out.candidates[limit - 1] = cand;
      }

      for (int i = static_cast<int>(out.count) - 1; i > 0; --i)
      {
        if (out.candidates[i].score > out.candidates[i - 1].score)
        {
          IRDecodeCandidate tmp = out.candidates[i - 1];
          out.candidates[i - 1] = out.candidates[i];
          out.candidates[i] = tmp;
        }
        else
        {
          break;
        }
      }
    }
  } // namespace detail

  template <size_t MaxCandidates>
  bool decodeRawToBits(const IRRawTickView &raw,
                       const IRProtocolSpec *specs,
                       size_t spec_count,
                       uint8_t max_candidates,
                       IRReceiveResult<MaxCandidates> &out)
  {
    out.count = 0;
    if (!specs || spec_count == 0)
      return false;
    if (!raw.ticks || raw.len == 0)
      return false;
    if (max_candidates == 0)
      return false;

    for (size_t i = 0; i < spec_count; ++i)
    {
      const IRProtocolSpec &spec = specs[i];
      if (spec.scheme != IRProtocolScheme::SPACE_ENC)
        continue;

      IRDecodedBits decoded{};
      int16_t score = 0;
      bool ok = detail::decodeNormal(raw, spec, decoded, score);
      if (!ok && spec.has_repeat)
      {
        ok = detail::decodeRepeat(raw, spec, decoded, score);
      }
      if (!ok)
        continue;

      IRDecodeCandidate cand{};
      cand.protocol_id = decoded.protocol_id;
      if (spec.name[0] != '\0')
      {
        std::strncpy(cand.name, spec.name, sizeof(cand.name) - 1);
        cand.name[sizeof(cand.name) - 1] = '\0';
      }
      cand.score = score;
      cand.decoded = decoded;
      detail::insertCandidate(out, max_candidates, cand);
    }

    return out.count > 0;
  }

} // namespace esp32irpk::codec
