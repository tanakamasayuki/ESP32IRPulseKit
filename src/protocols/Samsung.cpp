#include "../ESP32IRPulseKit.h"

namespace esp32irpk::specs
{

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

} // namespace esp32irpk::specs
