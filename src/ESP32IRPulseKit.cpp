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

  // ---- Built-in specs (placeholders) ----------------------------------------

  namespace specs
  {

    const IRProtocolSpec NEC = {
        .protocol_id = IRProtocolID::NEC,
        .scheme = IRProtocolScheme::SPACE_ENC,
        .family = IRProtocolFamily::NEC_LIKE,
        .header = {9000, 4500},
        .one = {560, 1690},
        .zero = {560, 560},
        .trailer = {560, 0},
        .frame_end_gap_us = 30000,
        .lsb_first = true,
        .bit_length = 32,
        .has_repeat = true,
        .repeat_header = {9000, 2250},
        .repeat_gap_us = 110000,
        .bit_tol_pct = 25,
        .endgap_tol_pct = 30,
    };

    const IRProtocolSpec SONY12 = {
        .protocol_id = IRProtocolID::SONY12,
        .scheme = IRProtocolScheme::SPACE_ENC,
        .family = IRProtocolFamily::UNKNOWN,
        .header = {2400, 600},
        .one = {1200, 600},
        .zero = {600, 600},
        .trailer = {0, 0},
        .frame_end_gap_us = 45000,
        .lsb_first = true,
        .bit_length = 12,
    };

    const IRProtocolSpec SONY15 = {
        .protocol_id = IRProtocolID::SONY15,
        .scheme = IRProtocolScheme::SPACE_ENC,
        .family = IRProtocolFamily::UNKNOWN,
        .header = {2400, 600},
        .one = {1200, 600},
        .zero = {600, 600},
        .trailer = {0, 0},
        .frame_end_gap_us = 45000,
        .lsb_first = true,
        .bit_length = 15,
    };

    const IRProtocolSpec SONY20 = {
        .protocol_id = IRProtocolID::SONY20,
        .scheme = IRProtocolScheme::SPACE_ENC,
        .family = IRProtocolFamily::UNKNOWN,
        .header = {2400, 600},
        .one = {1200, 600},
        .zero = {600, 600},
        .trailer = {0, 0},
        .frame_end_gap_us = 45000,
        .lsb_first = true,
        .bit_length = 20,
    };

    const IRProtocolSpec SAMSUNG32 = {
        .protocol_id = IRProtocolID::SAMSUNG32,
        .scheme = IRProtocolScheme::SPACE_ENC,
        .family = IRProtocolFamily::NEC_LIKE,
        .header = {4500, 4500},
        .one = {560, 1690},
        .zero = {560, 560},
        .trailer = {560, 0},
        .frame_end_gap_us = 30000,
        .lsb_first = true,
        .bit_length = 32,
    };

    const IRProtocolSpec SAMSUNG36 = {
        .protocol_id = IRProtocolID::SAMSUNG36,
        .scheme = IRProtocolScheme::SPACE_ENC,
        .family = IRProtocolFamily::NEC_LIKE,
        .header = {4500, 4500},
        .one = {560, 1690},
        .zero = {560, 560},
        .trailer = {560, 0},
        .frame_end_gap_us = 30000,
        .lsb_first = true,
        .bit_length = 36,
    };

  } // namespace specs

} // namespace esp32irpk
