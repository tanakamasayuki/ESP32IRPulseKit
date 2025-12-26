#pragma once

#include "Decoder.h"

namespace esp32irpk
{

  template <size_t MaxCandidates>
  IRReceiver<MaxCandidates>::IRReceiver(int gpio) : gpio_(gpio), inverted_(false) {}

  template <size_t MaxCandidates>
  IRReceiver<MaxCandidates>::IRReceiver(int gpio, bool inverted)
      : gpio_(gpio), inverted_(inverted) {}

  template <size_t MaxCandidates>
  bool IRReceiver<MaxCandidates>::setPin(int gpio)
  {
    if (begun_)
      return false;
    gpio_ = gpio;
    return true;
  }

  template <size_t MaxCandidates>
  bool IRReceiver<MaxCandidates>::setInverted(bool inverted)
  {
    if (begun_)
      return false;
    inverted_ = inverted;
    return true;
  }

  template <size_t MaxCandidates>
  bool IRReceiver<MaxCandidates>::setDecodeCandidates(uint8_t n)
  {
    if (begun_)
      return false;
    if (n > MaxCandidates)
      return false;
    decode_candidates_ = n;
    return true;
  }

  template <size_t MaxCandidates>
  bool IRReceiver<MaxCandidates>::setIdleThresholdUs(uint32_t us)
  {
    if (begun_)
      return false;
    idle_threshold_us_ = us;
    return true;
  }

  template <size_t MaxCandidates>
  bool IRReceiver<MaxCandidates>::addProtocol(const IRProtocolSpec &spec)
  {
    if (begun_)
      return false;
    IRProtocolSpec copy = spec;
    copy.order = order_counter_++;
    auto it = std::find_if(protocols_.begin(), protocols_.end(),
                           [&](const IRProtocolSpec &item)
                           { return item.protocol_id == spec.protocol_id; });
    if (it != protocols_.end())
    {
      *it = copy;
      return true;
    }
    protocols_.push_back(copy);
    return true;
  }

  template <size_t MaxCandidates>
  bool IRReceiver<MaxCandidates>::clearProtocols()
  {
    if (begun_)
      return false;
    protocols_.clear();
    order_counter_ = 0;
    return true;
  }

  template <size_t MaxCandidates>
  bool IRReceiver<MaxCandidates>::begin()
  {
    if (begun_)
      return false;

    if (decode_candidates_ > 0 && protocols_.empty())
      detail::addDefaultProtocols(protocols_);
    for (auto &spec : protocols_)
    {
      spec.order = order_counter_++;
    }

    uint32_t idle_threshold = idle_threshold_us_;
    if (decode_candidates_ > 0)
    {
      for (const auto &spec : protocols_)
      {
        if (spec.frame_end_gap_us == 0)
          continue;
        uint32_t tol = spec.endgap_tol_pct;
        uint64_t upper_gap = static_cast<uint64_t>(spec.frame_end_gap_us) * (100ULL + static_cast<uint64_t>(tol));
        upper_gap = (upper_gap + 99ULL) / 100ULL; // ceil
        if (upper_gap > idle_threshold)
          idle_threshold = static_cast<uint32_t>(upper_gap);
      }
    }

    if (!rmt_rx_.begin(gpio_, inverted_, idle_threshold))
      return false;
    begun_ = true;
    return true;
  }

  template <size_t MaxCandidates>
  void IRReceiver<MaxCandidates>::end()
  {
    rmt_rx_.end();
    begun_ = false;
  }

  template <size_t MaxCandidates>
  bool IRReceiver<MaxCandidates>::read(IRReceiveResult<MaxCandidates> &out)
  {
    if (!begun_)
      return false;

    out.flags = IRResultFlags::NONE;
    out.count = 0;
    if (!rmt_rx_.read(out.raw))
      return false;

    if (decode_candidates_ == 0)
    {
      out.flags |= IRResultFlags::DECODE_SKIPPED;
      rmt_rx_.consume(out.raw.len);
      stats_.queue_overflow_count += rmt_rx_.consumeQueueOverflowCount();
      if (rmt_rx_.consumeTruncatedFlag())
      {
        out.flags |= IRResultFlags::RAW_TRUNCATED;
        stats_.raw_truncated_count++;
      }
      if (rmt_rx_.consumeOverflowFlag())
      {
        out.flags |= IRResultFlags::RMT_OVERFLOW;
        stats_.rmt_overflow_count++;
      }
      return true;
    }

    decode(out.raw, out);

    size_t consumed = out.raw.len;
    if (out.count > 0 && out.candidates[0].consumed_len > 0)
      consumed = out.candidates[0].consumed_len;
    rmt_rx_.consume(consumed);

    stats_.queue_overflow_count += rmt_rx_.consumeQueueOverflowCount();
    if (rmt_rx_.consumeTruncatedFlag())
    {
      out.flags |= IRResultFlags::RAW_TRUNCATED;
      stats_.raw_truncated_count++;
    }
    if (rmt_rx_.consumeOverflowFlag())
    {
      out.flags |= IRResultFlags::RMT_OVERFLOW;
      stats_.rmt_overflow_count++;
    }
    return true;
  }

  template <size_t MaxCandidates>
  bool IRReceiver<MaxCandidates>::decode(const IRRawTickView &raw,
                                         IRReceiveResult<MaxCandidates> &out) const
  {
    out.count = 0;
    out.raw = raw;
    bool ok = codec::decodeRawToBits(raw,
                                     protocols_.data(),
                                     protocols_.size(),
                                     decode_candidates_,
                                     out);
    return ok;
  }

  template <size_t MaxCandidates>
  IRRxStats IRReceiver<MaxCandidates>::stats() const
  {
    return stats_;
  }

  template <size_t MaxCandidates>
  void IRReceiver<MaxCandidates>::resetStats()
  {
    stats_ = IRRxStats{};
  }

} // namespace esp32irpk
