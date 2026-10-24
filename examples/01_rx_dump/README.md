# 01_rx_dump — Receive and inspect protocol candidates

> 日本語版: [README.ja.md](README.ja.md)

Capture a remote signal, print its RAW timings, and inspect every decoded candidate and named field. Start here before choosing a transmit example.

## Prepare and run

1. Follow the [example guide](../../docs/GUIDE_EXAMPLES.md) for installation and wiring.
2. Connect power, GND and OUT of a demodulating IR receiver; match the input GPIO and inversion setting.
3. Open [01_rx_dump.ino](01_rx_dump.ino), select your board and upload. Open the Serial monitor at 115200 baud.

## Walk through the code

1. `IRReceiver rx(32, true)` selects the receiver OUT pin and inverts a typical active-low demodulated signal. Match both arguments to the module and board.
2. `rx.begin()` starts capture; `rx.read(r)` retrieves an available result. A false read normally means no completed capture is ready; it also returns false if initialization did not succeed.
3. `printRawMicros()` formats tick values as microseconds. The candidate loop calls `printDecodedCandidate()` for the score and bits, then `printDecodedFrame()` for protocol-specific fields.

## Check the result

Press the same button briefly several times. Look for a consistent protocol and command. Holding the button can produce repeat frames; a result with no candidates can still contain a captured RAW waveform.

## Adapt the example

The commented `setScoreThreshold()` line relaxes candidate filtering for investigation; a candidate score is a ranking, not a probability or a guarantee that an appliance will accept the command. Keep RAW diagnostics when comparing ambiguous protocols.

`IRReceiveResult::raw` is a view into receiver-owned storage. Copy ticks to your own array if they must survive another `read()` or `end()`. Where this sketch does not print the `begin()` result, check it first when reception never starts.

[Example index](../README.md) · [Library README](../../README.md)
