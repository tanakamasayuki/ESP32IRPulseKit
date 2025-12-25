#pragma once

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
  bool IRReceiver<MaxCandidates>::addProtocol(const IRProtocolSpec &)
  {
    if (begun_)
      return false;
    return true;
  }

  template <size_t MaxCandidates>
  bool IRReceiver<MaxCandidates>::clearProtocols()
  {
    if (begun_)
      return false;
    return true;
  }

  template <size_t MaxCandidates>
  bool IRReceiver<MaxCandidates>::begin()
  {
    begun_ = true;
    return true;
  }

  template <size_t MaxCandidates>
  void IRReceiver<MaxCandidates>::end()
  {
    begun_ = false;
  }

  template <size_t MaxCandidates>
  bool IRReceiver<MaxCandidates>::read(IRReceiveResult<MaxCandidates> &out)
  {
    (void)out;
    return false;
  }

  template <size_t MaxCandidates>
  bool IRReceiver<MaxCandidates>::decode(const IRRawTickView &raw,
                                         IRReceiveResult<MaxCandidates> &out) const
  {
    (void)raw;
    (void)out;
    return false;
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
