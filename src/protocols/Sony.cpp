#include "../ESP32IRPulseKit.h"

namespace esp32irpk::specs
{

    const IRProtocolSpec SONY12 = {
        .protocol_id = IRProtocolID::SONY12,
        .scheme = IRProtocolScheme::SPACE_ENC,
        .family = IRProtocolFamily::UNKNOWN,
        .header = {.mark_us = 2400, .space_us = 600},
        .one = {.mark_us = 1200, .space_us = 600},
        .zero = {.mark_us = 600, .space_us = 600},
        .trailer = {.mark_us = 0, .space_us = 0},
        .frame_end_gap_us = 45000,
        .lsb_first = true,
        .bit_length = 12,
    };

    const IRProtocolSpec SONY15 = {
        .protocol_id = IRProtocolID::SONY15,
        .scheme = IRProtocolScheme::SPACE_ENC,
        .family = IRProtocolFamily::UNKNOWN,
        .header = {.mark_us = 2400, .space_us = 600},
        .one = {.mark_us = 1200, .space_us = 600},
        .zero = {.mark_us = 600, .space_us = 600},
        .trailer = {.mark_us = 0, .space_us = 0},
        .frame_end_gap_us = 45000,
        .lsb_first = true,
        .bit_length = 15,
    };

    const IRProtocolSpec SONY20 = {
        .protocol_id = IRProtocolID::SONY20,
        .scheme = IRProtocolScheme::SPACE_ENC,
        .family = IRProtocolFamily::UNKNOWN,
        .header = {.mark_us = 2400, .space_us = 600},
        .one = {.mark_us = 1200, .space_us = 600},
        .zero = {.mark_us = 600, .space_us = 600},
        .trailer = {.mark_us = 0, .space_us = 0},
        .frame_end_gap_us = 45000,
        .lsb_first = true,
        .bit_length = 20,
    };

} // namespace esp32irpk::specs
