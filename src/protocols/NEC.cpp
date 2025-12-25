#include "../ESP32IRPulseKit.h"

namespace esp32irpk::specs
{

    // clang-format off
    const IRProtocolSpec NEC = {
        .protocol_id      = IRProtocolID::NEC,
        .scheme           = IRProtocolScheme::SPACE_ENC,
        .family           = IRProtocolFamily::NEC_LIKE,
        .header           = {.mark_us =  9000, .space_us = 4500},
        .one              = {.mark_us =   560, .space_us = 1690},
        .zero             = {.mark_us =   560, .space_us =  560},
        .trailer          = {.mark_us =   560, .space_us =    0},
        .frame_end_gap_us = 30000,
        .lsb_first        = true,
        .bit_length       = 32,
        .has_repeat       = true,
        .repeat_header    = {.mark_us =  9000, .space_us = 2250},
        .repeat_gap_us    = 110000,
        .bit_tol_pct      = 25,
        .endgap_tol_pct   = 30,
    };
    // clang-format on

} // namespace esp32irpk::specs
