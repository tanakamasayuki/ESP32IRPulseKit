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

  bool IRSender::addProtocol(const IRProtocolSpec &spec)
  {
    if (begun_)
      return false;
    auto it = std::find_if(protocols_.begin(), protocols_.end(),
                           [&](const IRProtocolSpec &item)
                           { return item.protocol_id == spec.protocol_id; });
    if (it != protocols_.end())
    {
      *it = spec;
      return true;
    }
    protocols_.push_back(spec);
    return true;
  }

  bool IRSender::clearProtocols()
  {
    if (begun_)
      return false;
    protocols_.clear();
    return true;
  }

  bool IRSender::begin()
  {
    if (begun_)
      return false;
    detail::addDefaultProtocols(protocols_);
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

  bool IRSender::send(const esp32irpk::IRRawTickView &raw, uint8_t repeat_count)
  {
    if (!begun_)
      return false;
    if (!raw.ticks || raw.len == 0)
      return false;
    return rmt_tx_.send(raw, repeat_count);
  }

  bool IRSender::send(const esp32irpk::IRRawTickView *raw, uint8_t repeat_count)
  {
    if (!raw)
      return false;
    return send(*raw, repeat_count);
  }

  bool IRSender::send(const IRDecodedBits &decoded, uint8_t repeat_count)
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
    return send(view, repeat_count);
  }

  bool IRSender::send(const IRDecodedBits *decoded, uint8_t repeat_count)
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

  bool IRSender::sendNEC(uint16_t address, uint8_t command, bool repeat)
  {
    if (!begun_)
      return false;

    IRDecodedBits bits{};
    bits.protocol_id = IRProtocolID::NEC;
    bits.frame_type = repeat ? IRFrameType::REPEAT : IRFrameType::NORMAL;
    if (repeat)
    {
      bits.bit_length = 0;
      bits.bits = 0xFFFFFFFFFFFFFFFFULL;
    }
    else
    {
      bits.bit_length = 32;
      uint64_t v = 0;
      v |= static_cast<uint64_t>(address);
      v |= (static_cast<uint64_t>(command) << 16);
      v |= (static_cast<uint64_t>(~command) << 24);
      bits.bits = v;
    }
    return send(bits, 0);
  }

  // ---- Receiver template instantiation (inline in codec/Receiver.inl) ----

} // namespace esp32irpk
