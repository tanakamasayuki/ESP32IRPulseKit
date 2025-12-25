#include "../ESP32IRPulseKit.h"

namespace esp32irpk::specs
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

} // namespace esp32irpk::specs
