#include "../ESP32IRPulseKit.h"

namespace esp32irpk::specs
{

    // clang-format off
    const IRProtocolSpec SAMSUNG32 = {
        .protocol_id      = IRProtocolID::SAMSUNG32,
        .scheme           = IRProtocolScheme::SPACE_ENC,
        .family           = IRProtocolFamily::NEC_LIKE,
        .header           = {.mark_us = 4500, .space_us = 4500},
        .one              = {.mark_us =  560, .space_us = 1690},
        .zero             = {.mark_us =  560, .space_us =  560},
        .trailer          = {.mark_us =  560, .space_us =    0},
        .frame_end_gap_us = 30000,
        .lsb_first        = true,
        .bit_length       = 32,
    };

    const IRProtocolSpec SAMSUNG36 = {
        .protocol_id      = IRProtocolID::SAMSUNG36,
        .scheme           = IRProtocolScheme::SPACE_ENC,
        .family           = IRProtocolFamily::NEC_LIKE,
        .header           = {.mark_us = 4500, .space_us = 4500},
        .one              = {.mark_us =  560, .space_us = 1690},
        .zero             = {.mark_us =  560, .space_us =  560},
        .trailer          = {.mark_us =  560, .space_us =    0},
        .frame_end_gap_us = 30000,
        .lsb_first        = true,
        .bit_length       = 36,
    };
    // clang-format on

} // namespace esp32irpk::specs
