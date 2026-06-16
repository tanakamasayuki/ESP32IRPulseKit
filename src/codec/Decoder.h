#pragma once

#include "../ESP32IRPulseKit.h"
#include <algorithm>
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

    inline int16_t finalizeWeightedScore(uint32_t header_err,
                                         uint32_t body_err,
                                         uint32_t extra_err = 0)
    {
      uint64_t weighted = static_cast<uint64_t>(header_err) * 8ULL +
                          static_cast<uint64_t>(body_err) +
                          static_cast<uint64_t>(extra_err);
      // scale down to reduce over-penalizing while keeping header impact higher
      uint32_t scaled = static_cast<uint32_t>((weighted + 3ULL) / 4ULL);
      return finalizeScore(scaled);
    }

    inline void adjustFamilyScore(const IRProtocolSpec &spec,
                                  const IRDecodedBits &decoded,
                                  int16_t &score)
    {
      switch (spec.family)
      {
      case IRProtocolFamily::AEHA:
      {
        if (decoded.bit_length >= 20)
        {
          uint16_t customer = static_cast<uint16_t>(decoded.bits & 0xFFFFu);
          uint8_t parity = static_cast<uint8_t>((decoded.bits >> 16) & 0xFu);
          uint8_t expected = static_cast<uint8_t>((customer ^ (customer >> 4) ^ (customer >> 8) ^ (customer >> 12)) & 0xFu);
          if (parity == expected)
          {
            score += 20;
          }
          else
          {
            score -= 40;
          }
        }
        break;
      }
      case IRProtocolFamily::PANASONIC:
      {
        // No special check for now
        break;
      }
      default:
        break;
      }
    }

    inline size_t maybeTrimByGap(const IRRawTickView &raw, const IRProtocolSpec &spec)
    {
      if (spec.gap_threshold_us == 0 || raw.len == 0)
        return raw.len;

      uint32_t gap_ticks = (spec.gap_threshold_us + (kTickUs - 1)) / kTickUs;

      // minimal symbols to consider a complete frame before gap:
      size_t min_symbols = 0;
      if (spec.header.mark_us)
        min_symbols += 1;
      if (spec.header.space_us)
        min_symbols += 1;
      if (spec.trailer.mark_us)
        min_symbols += 1;
      if (spec.trailer.space_us)
        min_symbols += 1;
      uint16_t max_bits = spec.max_bit_length ? spec.max_bit_length : spec.bit_length;
      min_symbols += static_cast<size_t>(max_bits) * 2;

      size_t symbols = raw.len;
      for (size_t i = 0; i < raw.len; ++i)
      {
        if ((i % 2) == 1) // space index
        {
          if (raw.ticks[i] >= gap_ticks && i + 1 >= min_symbols)
          {
            symbols = i + 1;
            break;
          }
        }
      }
      return symbols;
    }

    inline bool decodeNormal(const IRRawTickView &raw,
                             const IRProtocolSpec &spec,
                             IRDecodedBits &decoded,
                             int16_t &score_out)
    {
      uint16_t min_bits = spec.min_bit_length ? spec.min_bit_length : spec.bit_length;
      uint16_t max_bits = spec.max_bit_length ? spec.max_bit_length : spec.bit_length;
      if (min_bits == 0 || max_bits == 0 || min_bits > max_bits)
        return false;
      size_t header_syms = 0;
      if (spec.header.mark_us)
        header_syms++;
      if (spec.header.space_us)
        header_syms++;
      size_t trailer_syms = 0;
      if (spec.trailer.mark_us)
        trailer_syms++;
      if (spec.trailer.space_us)
        trailer_syms++;
      if (raw.len < header_syms + trailer_syms + static_cast<size_t>(min_bits) * 2)
        return false;

      size_t bit_syms = raw.len - header_syms - trailer_syms;
      if (bit_syms % 2 != 0)
        return false;
      size_t bit_count_sz = bit_syms / 2;
      if (bit_count_sz < min_bits || bit_count_sz > max_bits)
        return false;
      uint16_t bit_count = static_cast<uint16_t>(bit_count_sz);
      size_t idx = 0;
      uint32_t header_err = 0;
      uint32_t body_err = 0;
      uint32_t extra_err = 0;

      if (!consumePulse(raw, idx, spec.header.mark_us, spec.bit_tol_pct, header_err) ||
          !consumePulse(raw, idx, spec.header.space_us, spec.bit_tol_pct, header_err))
        return false;

      uint64_t bits = 0;
      for (uint16_t i = 0; i < bit_count; ++i)
      {
        uint32_t mark_us = ticksToUs(raw.ticks[idx]);
        uint32_t space_us = 0;
        bool has_space = (idx + 1 < raw.len);
        if (has_space)
          space_us = ticksToUs(raw.ticks[idx + 1]);

        bool is_last_bit = (i + 1 == bit_count);
        bool space_is_gap = false;
        if (is_last_bit)
        {
          if (has_space && spec.gap_threshold_us > 0)
            space_is_gap = space_us >= spec.gap_threshold_us;
          else if (!has_space)
            space_is_gap = true; // allow clipped gap
        }
        else if (!has_space)
        {
          return false;
        }

        PulseMatch one_mark = matchPulse(mark_us, spec.one.mark_us, spec.bit_tol_pct);
        PulseMatch one_space = space_is_gap ? PulseMatch{true, 0}
                                            : matchPulse(space_us, spec.one.space_us, spec.bit_tol_pct);
        PulseMatch zero_mark = matchPulse(mark_us, spec.zero.mark_us, spec.bit_tol_pct);
        PulseMatch zero_space = space_is_gap ? PulseMatch{true, 0}
                                             : matchPulse(space_us, spec.zero.space_us, spec.bit_tol_pct);

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
            bits |= (1ULL << (bit_count - 1 - i));
        }

        body_err += bit_err;
        idx += 2;
      }

      if (!consumePulse(raw, idx, spec.trailer.mark_us, spec.bit_tol_pct, header_err) ||
          !consumePulse(raw, idx, spec.trailer.space_us, spec.bit_tol_pct, header_err))
        return false;

      if (idx < raw.len)
      {
        if (raw.len - idx == 1)
        {
          // trailing gap: ignore without penalty
          ++idx;
        }
        else
        {
          return false;
        }
      }

      decoded.protocol_id = spec.protocol_id;
      decoded.frame_type = IRFrameType::NORMAL;
      decoded.bit_length = bit_count;
      decoded.bits = bits;

      uint64_t weighted = static_cast<uint64_t>(header_err) * 8ULL +
                          static_cast<uint64_t>(body_err) +
                          static_cast<uint64_t>(extra_err);
      uint32_t scaled = static_cast<uint32_t>((weighted + 3ULL) / 4ULL);
      score_out = finalizeScore(scaled);
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
      uint32_t header_err = 0;
      uint32_t body_err = 0;
      uint32_t extra_err = 0;

      if (!consumePulse(raw, idx, spec.repeat_header.mark_us, spec.bit_tol_pct, header_err) ||
          !consumePulse(raw, idx, spec.repeat_header.space_us, spec.bit_tol_pct, header_err))
        return false;

      if (!consumePulse(raw, idx, spec.trailer.mark_us, spec.bit_tol_pct, header_err) ||
          !consumePulse(raw, idx, spec.trailer.space_us, spec.bit_tol_pct, header_err))
        return false;

      if (idx < raw.len)
      {
        if (raw.len - idx == 1)
        {
          uint32_t gap_us = ticksToUs(raw.ticks[idx]);
          uint32_t expected_gap = 0;
          if (spec.repeat_gap_us > 0)
          {
            uint64_t consumed_us = 0;
            for (size_t j = 0; j < idx; ++j)
              consumed_us += ticksToUs(raw.ticks[j]);
            if (consumed_us < spec.repeat_gap_us)
              expected_gap = static_cast<uint32_t>(spec.repeat_gap_us - consumed_us);
          }
          else if (spec.gap_threshold_us > 0)
          {
            expected_gap = spec.gap_threshold_us;
          }
          if (expected_gap > 0)
            extra_err += errorPct(gap_us, expected_gap);
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

      uint64_t weighted = static_cast<uint64_t>(header_err) * 8ULL +
                          static_cast<uint64_t>(body_err) +
                          static_cast<uint64_t>(extra_err);
      uint32_t scaled = static_cast<uint32_t>((weighted + 3ULL) / 4ULL);
      score_out = finalizeScore(scaled);
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
        if (cand.score < out.candidates[limit - 1].score)
          return;
        if (cand.score == out.candidates[limit - 1].score &&
            cand.order >= out.candidates[limit - 1].order)
          return;
        out.candidates[limit - 1] = cand;
      }

      for (int i = static_cast<int>(out.count) - 1; i > 0; --i)
      {
        bool better = out.candidates[i].score > out.candidates[i - 1].score;
        if (!better && out.candidates[i].score == out.candidates[i - 1].score)
          better = out.candidates[i].order < out.candidates[i - 1].order;
        if (better)
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

  namespace rc_detail
  {
    inline bool appendHalves(uint32_t duration_us,
                             uint32_t unit_us,
                             uint16_t tol_pct,
                             bool &level,
                             std::vector<bool> &halves,
                             uint32_t &err_sum)
    {
      if (unit_us == 0)
        return false;
      uint32_t units = (duration_us + (unit_us / 2U)) / unit_us;
      if (units == 0)
        return false;
      uint32_t expected = units * unit_us;
      err_sum += detail::errorPct(duration_us, expected);
      for (uint32_t i = 0; i < units; ++i)
      {
        halves.push_back(level);
      }
      level = !level;
      return true;
    }

    inline bool buildHalves(const IRRawTickView &raw,
                            size_t start_idx,
                            uint32_t unit_us,
                            uint16_t tol_pct,
                            std::vector<bool> &halves,
                            uint32_t &err_sum)
    {
      bool level = true; // starts with mark
      for (size_t i = start_idx; i < raw.len; ++i)
      {
        uint32_t dur = detail::ticksToUs(raw.ticks[i]);
        if (!appendHalves(dur, unit_us, tol_pct, level, halves, err_sum))
          return false;
      }
      return true;
    }

    inline bool decodeManBit(const std::vector<bool> &halves,
                             size_t idx,
                             size_t half_count,
                             bool &bit_out)
    {
      if (idx + half_count > halves.size() || half_count < 2 || (half_count % 2) != 0)
        return false;
      bool first = halves[idx];
      bool mid = halves[idx + half_count / 2];
      if (first == mid)
        return false;
      bit_out = first; // high->low = 1, low->high = 0
      return true;
    }

    inline bool decodeRC5(const IRRawTickView &raw,
                          const IRProtocolSpec &spec,
                          IRDecodedBits &decoded,
                          int16_t &score_out)
    {
      const uint32_t unit_us = 889;
      const size_t bits = 14;
      if (raw.len < 2)
        return false;

      std::vector<bool> halves;
      uint32_t err_sum = 0;
      if (!buildHalves(raw, 0, unit_us, spec.bit_tol_pct, halves, err_sum))
        return false;
      if (halves.size() < bits * 2)
        return false;
      if (halves.size() > bits * 2)
        halves.resize(bits * 2);

      uint64_t bits_out = 0;
      for (size_t i = 0; i < bits; ++i)
      {
        bool bit = false;
        if (!decodeManBit(halves, i * 2, 2, bit))
          return false;
        size_t pos = spec.lsb_first ? i : (bits - 1 - i);
        if (bit)
          bits_out |= (1ULL << pos);
      }

      // Start bits (first two) should be 1
      if (!((bits_out >> (bits - 1)) & 0x1ULL))
        err_sum += 50;
      if (!((bits_out >> (bits - 2)) & 0x1ULL))
        err_sum += 50;

      decoded.protocol_id = spec.protocol_id;
      decoded.frame_type = IRFrameType::NORMAL;
      decoded.bit_length = bits;
      decoded.bits = bits_out;
      score_out = detail::finalizeScore(err_sum);
      return true;
    }

    inline bool decodeRC6Common(const IRRawTickView &raw,
                                const IRProtocolSpec &spec,
                                uint8_t mode_expect,
                                uint16_t payload_bits,
                                bool has_toggle,
                                IRDecodedBits &decoded,
                                int16_t &score_out)
    {
      const uint32_t unit_us = 444;
      if (raw.len < 2)
        return false;

      size_t idx = 0;
      uint32_t header_err = 0;
      // Leader: 6T mark + 2T space
      uint32_t lead_mark = detail::ticksToUs(raw.ticks[idx]);
      uint32_t lead_space = (idx + 1 < raw.len) ? detail::ticksToUs(raw.ticks[idx + 1]) : 0;
      header_err += detail::errorPct(lead_mark, unit_us * 6U);
      if (raw.ticks[idx] == 0 || raw.ticks[idx + 1] == 0)
        return false;
      header_err += detail::errorPct(lead_space, unit_us * 2U);
      idx += 2;

      std::vector<bool> halves;
      uint32_t body_err = 0;
      if (!buildHalves(raw, idx, unit_us, spec.bit_tol_pct, halves, body_err))
        return false;

      size_t start_bit_halves = 4; // double width
      size_t toggle_halves = has_toggle ? 4 : 0;
      size_t total_bits = 1 + 3 + (has_toggle ? 1 : 0) + payload_bits;
      size_t expected_halves = start_bit_halves + toggle_halves + (total_bits - 1 - (has_toggle ? 1 : 0)) * 2 + (has_toggle ? 0 : 0);
      if (halves.size() < expected_halves)
        return false;
      size_t pos = 0;

      auto decodeBitWidth = [&](size_t width_halves, bool &bit_out) -> bool
      {
        if (!decodeManBit(halves, pos, width_halves, bit_out))
          return false;
        pos += width_halves;
        return true;
      };

      uint64_t bits_out = 0;
      size_t bit_index = 0;

      bool bit = false;
      if (!decodeBitWidth(start_bit_halves, bit))
        return false;
      size_t target = spec.lsb_first ? bit_index : (total_bits - 1 - bit_index);
      if (bit)
        bits_out |= (1ULL << target);
      bit_index++;

      uint8_t mode = 0;
      for (int i = 0; i < 3; ++i, ++bit_index)
      {
        if (!decodeBitWidth(2, bit))
          return false;
        if (bit)
          mode |= (1U << (2 - i)); // MSB first for mode
        target = spec.lsb_first ? bit_index : (total_bits - 1 - bit_index);
        if (bit)
          bits_out |= (1ULL << target);
      }

      if (mode != mode_expect)
        body_err += 80;

      if (has_toggle)
      {
        if (!decodeBitWidth(toggle_halves, bit))
          return false;
        target = spec.lsb_first ? bit_index : (total_bits - 1 - bit_index);
        if (bit)
          bits_out |= (1ULL << target);
        ++bit_index;
      }

      for (uint16_t p = 0; p < payload_bits; ++p, ++bit_index)
      {
        if (!decodeBitWidth(2, bit))
          return false;
        target = spec.lsb_first ? bit_index : (total_bits - 1 - bit_index);
        if (bit)
          bits_out |= (1ULL << target);
      }

      decoded.protocol_id = spec.protocol_id;
      decoded.frame_type = IRFrameType::NORMAL;
      decoded.bit_length = static_cast<uint16_t>(total_bits);
      decoded.bits = bits_out;
      uint64_t weighted = static_cast<uint64_t>(header_err) * 8ULL +
                          static_cast<uint64_t>(body_err);
      uint32_t scaled = static_cast<uint32_t>((weighted + 3ULL) / 4ULL);
      score_out = detail::finalizeScore(scaled);
      return true;
    }

    inline bool decodeRC6M0(const IRRawTickView &raw,
                            const IRProtocolSpec &spec,
                            IRDecodedBits &decoded,
                            int16_t &score_out)
    {
      return decodeRC6Common(raw, spec, /*mode_expect=*/0, /*payload_bits=*/16, /*has_toggle=*/true, decoded, score_out);
    }

    inline bool decodeRC6M6(const IRRawTickView &raw,
                            const IRProtocolSpec &spec,
                            IRDecodedBits &decoded,
                            int16_t &score_out)
    {
      return decodeRC6Common(raw, spec, /*mode_expect=*/6, /*payload_bits=*/32, /*has_toggle=*/false, decoded, score_out);
    }
  } // namespace rc_detail

  template <size_t MaxCandidates>
  bool decodeRawToBits(const IRRawTickView &raw,
                       const IRProtocolSpec *specs,
                       size_t spec_count,
                       uint8_t max_candidates,
                       int16_t score_threshold,
                       IRReceiveResult<MaxCandidates> &out)
  {
    out.count = 0;
    if (!specs || spec_count == 0)
      return false;
    if (!raw.ticks || raw.len == 0)
      return false;
    if (max_candidates == 0)
      return false;

    out.raw = raw;

    for (size_t i = 0; i < spec_count; ++i)
    {
      const IRProtocolSpec &spec = specs[i];
      IRDecodedBits decoded{};
      int16_t score = 0;
      size_t effective_len = raw.len;
      bool ok = false;

      if (spec.scheme == IRProtocolScheme::SPACE_ENC)
      {
        effective_len = detail::maybeTrimByGap(raw, spec);
        size_t decode_len = effective_len;

        // Drop a trailing gap tick from the decode view if it would leave an odd bit symbol count.
        if (decode_len > 0)
        {
          size_t header_syms = 0;
          if (spec.header.mark_us)
            header_syms++;
          if (spec.header.space_us)
            header_syms++;
          size_t trailer_syms = 0;
          if (spec.trailer.mark_us)
            trailer_syms++;
          if (spec.trailer.space_us)
            trailer_syms++;

          if (decode_len > header_syms + trailer_syms)
          {
            size_t bit_syms = decode_len - header_syms - trailer_syms;
            bool last_is_space = ((decode_len % 2) == 0); // mark-first streams end on space when a gap is present
            if (last_is_space && (bit_syms % 2) != 0)
            {
              bool gap_candidate = (spec.gap_threshold_us == 0);
              uint32_t tail_us = detail::ticksToUs(raw.ticks[decode_len - 1]);
              if (!gap_candidate)
                gap_candidate = tail_us >= spec.gap_threshold_us;
              if (gap_candidate)
                --decode_len;
            }
          }
        }

        IRRawTickView sub_raw = raw;
        sub_raw.len = decode_len;

        ok = detail::decodeNormal(sub_raw, spec, decoded, score);
        if (!ok && spec.has_repeat)
        {
          ok = detail::decodeRepeat(sub_raw, spec, decoded, score);
        }
      }
      else if (spec.scheme == IRProtocolScheme::BIPHASE)
      {
        switch (spec.protocol_id)
        {
        case IRProtocolID::RC5:
          ok = rc_detail::decodeRC5(raw, spec, decoded, score);
          break;
        case IRProtocolID::RC6_M0_16:
          ok = rc_detail::decodeRC6M0(raw, spec, decoded, score);
          break;
        case IRProtocolID::RC6_M6_32:
          ok = rc_detail::decodeRC6M6(raw, spec, decoded, score);
          break;
        default:
          ok = false;
          break;
        }
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
      cand.order = spec.order;
      cand.consumed_len = effective_len;
      cand.score = score;
      cand.decoded = decoded;
      detail::adjustFamilyScore(spec, cand.decoded, cand.score);
      if (cand.score < score_threshold)
        continue;
      detail::insertCandidate(out, max_candidates, cand);
    }

    if (out.count > 0)
    {
      size_t consumed = out.candidates[0].consumed_len > 0 ? out.candidates[0].consumed_len
                                                           : out.raw.len;
      if (consumed < out.raw.len)
        out.raw.len = consumed;
    }

    return out.count > 0;
  }

} // namespace esp32irpk::codec
