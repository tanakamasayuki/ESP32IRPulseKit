#include "ESP32IRPulseKit.h"

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

  bool IRSender::addProtocol(const IRProtocolSpec &)
  {
    if (begun_)
      return false;
    return true;
  }

  bool IRSender::clearProtocols()
  {
    if (begun_)
      return false;
    return true;
  }

  bool IRSender::begin()
  {
    begun_ = true;
    return true;
  }

  void IRSender::end()
  {
    begun_ = false;
  }

  bool IRSender::send(const esp32irpk::IRRawTickView &raw, uint8_t repeat_count)
  {
    (void)raw;
    (void)repeat_count;
    if (!begun_)
      return false;
    return false;
  }

  bool IRSender::send(const esp32irpk::IRRawTickView *raw, uint8_t repeat_count)
  {
    if (!raw)
      return false;
    return send(*raw, repeat_count);
  }

  bool IRSender::send(const IRDecodedBits &decoded, uint8_t repeat_count)
  {
    (void)decoded;
    (void)repeat_count;
    if (!begun_)
      return false;
    return false;
  }

  bool IRSender::send(const IRDecodedBits *decoded, uint8_t repeat_count)
  {
    if (!decoded)
      return false;
    return send(*decoded, repeat_count);
  }

  bool IRSender::encode(const IRDecodedBits &decoded, IRRawTickBuffer &out_raw)
  {
    (void)decoded;
    (void)out_raw;
    if (!begun_)
      return false;
    return false;
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
    return send(bits, repeat ? 0 : 0);
  }

} // namespace esp32irpk
