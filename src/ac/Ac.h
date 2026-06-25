#pragma once

#include "../ESP32IRPulseKit.h"
#include "AcCodec.h"
#include "Panasonic.h"
#include "Gree.h"

// Air-conditioner support layer. AC frames are multi-byte vendor state that
// does not fit the generic 64-bit IRDecodedBits codec, so this layer works on
// the RAW tick path instead. See SPEC §11.

namespace esp32irpk::ac
{

  enum class AcVendor : uint16_t
  {
    UNKNOWN = 0,
    PANASONIC = 1,
    GREE = 2,
    // further vendors added incrementally
  };

  // One-call send: encode any vendor Frame into a stack buffer sized by
  // Frame::kMaxTicks and transmit it. Returns false on encode or send failure.
  // Configure the sender's carrier mode (setPhaseAlignedCarrier(false) for long
  // frames) separately, as usual.
  template <class Frame>
  bool send(esp32irpk::IRSender &tx, const Frame &frame)
  {
    uint16_t ticks[Frame::kMaxTicks];
    esp32irpk::IRRawTickBuffer buf{ticks, Frame::kMaxTicks, 0};
    if (!frame.toRaw(buf))
      return false;
    return tx.send(esp32irpk::IRRawTickView{buf.ticks, buf.len});
  }

} // namespace esp32irpk::ac
