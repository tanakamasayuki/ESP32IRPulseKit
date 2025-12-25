#include "../ESP32IRPulseKit.h"

namespace esp32irpk::specs
{

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

} // namespace esp32irpk::specs
