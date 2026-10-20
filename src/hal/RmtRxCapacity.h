#pragma once

#include <stddef.h>

// RX capture capacity planning, kept free of ESP-IDF types so it can be tested
// on the host build.
//
// A capture holds at most `max_symbols` RMT symbols (setMaxRxSymbols()). The
// receive buffer keeps one sentinel slot past that, so a longer frame is
// detectable (the driver never reports more symbols than the buffer holds).
//
// Chips with RX ping-pong (ESP32-S3/C3/C6/H2/P4) stream symbols from a small
// hardware memory into the buffer, so only the buffer limits a capture. Chips
// without it (ESP32, ESP32-S2) need the whole frame in the channel's hardware
// memory, so the RX channel is given several RMT memory blocks (see
// rxMaxMemBlocks() for the limit).
namespace esp32irpk::hal
{

  // Blocks an RX channel may take on chips without RX ping-pong. With the
  // default capacity: at most half of the shared RMT memory, leaving room for
  // other RMT users (IR TX, WS2812 LEDs, ...). With a capacity set explicitly
  // by setMaxRxSymbols(): all but one block, so one TX channel can still coexist.
  constexpr size_t rxMaxMemBlocks(size_t total_blocks, bool explicit_capacity = false)
  {
    return explicit_capacity ? (total_blocks > 1 ? total_blocks - 1 : 1)
                             : (total_blocks / 2 > 0 ? total_blocks / 2 : 1);
  }

  // Blocks needed to hold the capacity plus the sentinel, clamped to [1, max_blocks].
  constexpr size_t rxMemBlocksFor(size_t max_symbols, size_t words_per_block, size_t max_blocks)
  {
    const size_t want = (max_symbols + 1 + words_per_block - 1) / words_per_block;
    return want < 1 ? 1 : (want > max_blocks ? max_blocks : want);
  }

  constexpr size_t rxBufferSymbols(size_t max_symbols)
  {
    return max_symbols + 1;
  }

  // hw_symbols: RX hardware memory when the frame must fit in it (no ping-pong),
  // 0 when the driver streams (ping-pong). A capture that fills the hardware
  // memory was cut by it, not ended by the idle gap.
  constexpr bool rxCaptureTruncated(size_t received, size_t max_symbols, size_t hw_symbols)
  {
    return received > max_symbols || (hw_symbols > 0 && received >= hw_symbols);
  }

  constexpr size_t rxCapturedSymbols(size_t received, size_t max_symbols)
  {
    return received > max_symbols ? max_symbols : received;
  }

} // namespace esp32irpk::hal
