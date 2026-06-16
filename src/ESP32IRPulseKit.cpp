#include "ESP32IRPulseKit.h"
#include "hal/RmtHal.h"
#include "codec/Encoder.h"
#include "codec/Decoder.h"

namespace esp32irpk
{

  // ---- IRSender --------------------------------------------------------------

  IRSender::IRSender(int gpio) : gpio_(gpio), inverted_(false) {}

  IRSender::IRSender(int gpio, bool inverted) : gpio_(gpio), inverted_(inverted) {}

  bool IRSender::setPin(int gpio)
  {
    if (begun_)
      return false;
    gpio_ = gpio;
    return true;
  }

  bool IRSender::setInverted(bool inverted)
  {
    if (begun_)
      return false;
    inverted_ = inverted;
    return true;
  }

  namespace
  {
    constexpr uint32_t kMinCarrierHz = 20000;
    constexpr uint32_t kMaxCarrierHz = 60000;

    bool isValidCarrierHz(uint32_t hz)
    {
      return hz >= kMinCarrierHz && hz <= kMaxCarrierHz;
    }
  } // namespace

  bool IRSender::setCarrierHz(uint32_t hz)
  {
    if (hz == 0)
      return clearCarrierHz();
    if (!isValidCarrierHz(hz))
      return false;
    if (begun_ && rmt_tx_.isSending())
      return false;

    if (begun_ && !rmt_tx_.applyCarrierHz(hz))
      return false;

    carrier_override_ = true;
    carrier_hz_ = hz;
    return true;
  }

  bool IRSender::clearCarrierHz()
  {
    if (begun_ && rmt_tx_.isSending())
      return false;

    if (begun_ && !rmt_tx_.applyCarrierHz(kDefaultCarrierHz))
      return false;

    carrier_override_ = false;
    carrier_hz_ = kDefaultCarrierHz;
    return true;
  }

  bool IRSender::addProtocol(const IRProtocolSpec &spec)
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

  bool IRSender::clearProtocols()
  {
    if (begun_)
      return false;
    protocols_.clear();
    order_counter_ = 0;
    return true;
  }

  bool IRSender::begin()
  {
    if (begun_)
      return false;
    detail::addDefaultProtocols(protocols_);
    for (auto &spec : protocols_)
    {
      spec.order = order_counter_++;
    }
    if (!rmt_tx_.begin(gpio_, inverted_))
      return false;
    begun_ = true;
    return true;
  }

  void IRSender::end()
  {
    rmt_tx_.end();
    begun_ = false;
  }

  namespace
  {
    inline uint8_t resolveRepeatCount(const std::vector<IRProtocolSpec> &specs,
                                      IRProtocolID id,
                                      int8_t repeat_count)
    {
      if (repeat_count >= 0)
        return static_cast<uint8_t>(repeat_count);

      int8_t def = 0;
      auto it = std::find_if(specs.begin(), specs.end(),
                             [&](const IRProtocolSpec &s)
                             { return s.protocol_id == id; });
      if (it != specs.end())
        def = it->default_repeat_count;
      if (def < 0)
        def = 0;
      return static_cast<uint8_t>(def);
    }

    inline uint32_t resolveCarrierHz(const std::vector<IRProtocolSpec> &specs,
                                     IRProtocolID id,
                                     bool carrier_override,
                                     uint32_t carrier_hz)
    {
      if (carrier_override)
        return carrier_hz;

      auto it = std::find_if(specs.begin(), specs.end(),
                             [&](const IRProtocolSpec &s)
                             { return s.protocol_id == id; });
      if (it != specs.end() && it->carrier_hz != 0)
        return it->carrier_hz;
      return kDefaultCarrierHz;
    }
  } // namespace

  bool IRSender::send(const esp32irpk::IRRawTickView &raw, int8_t repeat_count)
  {
    if (!begun_)
      return false;
    if (!raw.ticks || raw.len == 0)
      return false;
    uint8_t resolved = repeat_count >= 0 ? static_cast<uint8_t>(repeat_count) : 0;
    if (resolved > 127U)
      resolved = 127U;
    return rmt_tx_.send(raw, static_cast<int8_t>(resolved), carrier_hz_);
  }

  bool IRSender::send(const esp32irpk::IRRawTickView *raw, int8_t repeat_count)
  {
    if (!raw)
      return false;
    return send(*raw, repeat_count);
  }

  bool IRSender::send(const IRDecodedBits &decoded, int8_t repeat_count)
  {
    if (!begun_)
      return false;
    IRRawTickBuffer out_raw{};
    out_raw.ticks = encode_buf_;
    out_raw.capacity = kMaxEncodedTicks;
    out_raw.len = 0;
    if (!codec::encodeBitsToRaw(decoded, protocols_.data(), protocols_.size(), out_raw))
      return false;

    IRRawTickView view{};
    view.ticks = out_raw.ticks;
    view.len = out_raw.len;
    uint8_t resolved = resolveRepeatCount(protocols_, decoded.protocol_id, repeat_count);
    if (resolved > 127U)
      resolved = 127U;
    uint32_t carrier = resolveCarrierHz(protocols_, decoded.protocol_id, carrier_override_, carrier_hz_);
    return rmt_tx_.send(view, static_cast<int8_t>(resolved), carrier);
  }

  bool IRSender::send(const IRDecodedBits *decoded, int8_t repeat_count)
  {
    if (!decoded)
      return false;
    return send(*decoded, repeat_count);
  }

  bool IRSender::encode(const IRDecodedBits &decoded, IRRawTickBuffer &out_raw)
  {
    if (!begun_)
      return false;
    return codec::encodeBitsToRaw(decoded, protocols_.data(), protocols_.size(), out_raw);
  }

  // ---- Receiver template instantiation (inline in codec/Receiver.inl) ----

} // namespace esp32irpk
