#pragma once

#include "../ESP32IRPulseKit.h"

// Generic pulse-distance (NEC/AEHA-like) tick<->byte conversion shared by AC
// vendors. AC frames are byte-structured and longer than the 64-bit generic
// codec, so this layer works on raw byte arrays instead of IRDecodedBits.
//
// NOTE: the bodies are stubs in this skeleton (Step 2). The real decode/encode
// is implemented in Step 3 alongside the Panasonic fixtures that verify it.

namespace esp32irpk::ac
{

  // Pulse-distance timing for one vendor frame: a leading header, each bit a
  // fixed mark plus a 0/1-length space, a trailing mark, and a long gap between
  // concatenated frames.
  struct AcTiming
  {
    uint16_t header_mark_us = 0;
    uint16_t header_space_us = 0;
    uint16_t bit_mark_us = 0;
    uint16_t zero_space_us = 0;
    uint16_t one_space_us = 0;
    uint16_t trailer_mark_us = 0;
    uint16_t frame_gap_us = 0; // gap separating concatenated frames
    uint8_t tol_pct = 30;      // matching tolerance
    bool lsb_first = true;     // bit order within each byte
  };

  // Decode one pulse-distance frame from `raw` starting at tick index `pos`.
  // On success returns the decoded bit count and advances `pos` past the frame;
  // returns 0 on mismatch.
  inline size_t rawFrameToBytes(const esp32irpk::IRRawTickView &raw, size_t &pos,
                                const AcTiming &t, uint8_t *out, size_t out_cap)
  {
    // TODO(Step 3): implement pulse-distance decode for AC frames.
    (void)raw;
    (void)pos;
    (void)t;
    (void)out;
    (void)out_cap;
    return 0;
  }

  // Encode `bit_len` bits from `bytes` as one pulse-distance frame appended to
  // `out`. Returns false on capacity overflow.
  inline bool bytesFrameToRaw(const uint8_t *bytes, size_t bit_len,
                              const AcTiming &t, esp32irpk::IRRawTickBuffer &out)
  {
    // TODO(Step 3): implement pulse-distance encode for AC frames.
    (void)bytes;
    (void)bit_len;
    (void)t;
    (void)out;
    return false;
  }

} // namespace esp32irpk::ac
