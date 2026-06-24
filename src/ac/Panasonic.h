#pragma once

#include "../ESP32IRPulseKit.h"
#include "AcCodec.h"

// Panasonic air-conditioner support. Per-vendor namespace with the common
// structure (Mode / Fan / Frame). See SPEC §11.2.
//
// NOTE: this is the Step 2 skeleton. Byte layout, field offsets, checksum, and
// timing are filled in during Step 3 from the Panasonic reverse engineering;
// the stub bodies below keep the public surface compilable until then.

namespace esp32irpk::ac::Panasonic
{

  // Per-vendor enums: only the values Panasonic supports. Common members use
  // the shared naming convention (AUTO/COOL/HEAT/DRY/FAN, ...).
  enum class Mode : uint8_t
  {
    AUTO = 0,
    COOL,
    HEAT,
    DRY,
    FAN,
  };

  // NOTE: Arduino defines `LOW`/`HIGH` as preprocessor macros (0/1), so bare
  // LOW/HIGH cannot be enumerators here. Use the `_SPEED` suffix (a single
  // token the `LOW`/`HIGH` macros do not match).
  enum class Fan : uint8_t
  {
    AUTO = 0,
    QUIET,
    LOW_SPEED,
    MED_SPEED,
    HIGH_SPEED,
    POWERFUL,
  };

  struct Frame
  {
    // TODO(Step 3): confirm the real state size and full two-frame burst length.
    static constexpr size_t kBytes = 27;
    static constexpr size_t kMaxTicks = 700;

    uint8_t bytes[kBytes] = {}; // raw decoded state (the intermediate form)
    uint16_t byte_length = 0;
    bool checksum_ok = false;

    // Logical accessors over `bytes`. TODO(Step 3): real bit/byte layout.
    bool power() const { return false; }
    void setPower(bool on) { (void)on; }
    Mode mode() const { return Mode::AUTO; }
    void setMode(Mode m) { (void)m; }
    uint8_t temperatureC() const { return 0; }
    void setTemperatureC(uint8_t c) { (void)c; }
    Fan fan() const { return Fan::AUTO; }
    void setFan(Fan f) { (void)f; }

    // RAW ticks -> state bytes (validates checksum). false if not a Panasonic
    // frame; checksum validity is reported separately via `checksum_ok`.
    static bool fromRaw(const esp32irpk::IRRawTickView &raw, Frame &out)
    {
      // TODO(Step 3): decode the two-frame Panasonic burst and verify checksum.
      (void)raw;
      (void)out;
      return false;
    }

    // state -> RAW ticks (recomputes checksum) into the caller's buffer.
    bool toRaw(esp32irpk::IRRawTickBuffer &out) const
    {
      // TODO(Step 3): render the two-frame Panasonic burst with checksum.
      (void)out;
      return false;
    }
  };

} // namespace esp32irpk::ac::Panasonic
