# 05_raw_monitor — Inspect RAW reception and dropped captures

> 日本語版: [README.ja.md](README.ja.md)

Disable protocol decoding, print captured durations, and inspect cumulative receiver statistics.

## Prepare and run

1. Follow the [example guide](../../docs/GUIDE_EXAMPLES.md) for installation and wiring.
2. Connect power, GND and OUT of a demodulating IR receiver; match the input GPIO and inversion setting.
3. Open [05_raw_monitor.ino](05_raw_monitor.ino), select your board and upload. Open the Serial monitor at 115200 baud.

## Walk through the code

1. `setDecodeCandidates(0)` before `begin()` selects RAW-only operation. A result with no decoded candidates is intentional in this mode.
2. `r.truncated()` means the waveform was cut to fit capture storage; `r.rmtOverflow()` reports an RMT overflow. Neither should be ignored when using the data for replay.
3. `rx.stats()` returns accumulated queue-drop, hardware-overflow and truncation counters. Compare successive values while varying remote activity and Serial output volume.

## Check the result

Each completed capture prints durations and statistics. First test isolated short presses, then longer bursts. Increasing counters explain missing or incomplete output even if some captures still succeed.

## Adapt the example

Reduce printing or process captures faster if queue drops rise. For long captures, configure `setMaxRxSymbols()` before `begin()` and choose an appropriate idle threshold; adding capacity alone cannot repair a burst split by an internal silence.

`IRReceiveResult::raw` is a view into receiver-owned storage. Copy ticks to your own array if they must survive another `read()` or `end()`. Where this sketch does not print the `begin()` result, check it first when reception never starts.

[Example index](../README.md) · [Library README](../../README.md)
