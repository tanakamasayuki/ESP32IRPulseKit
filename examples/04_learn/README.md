# 04_learn — Print reusable remote send code

> 日本語版: [README.ja.md](README.ja.md)

Receive a remote signal and print C++ code to copy into a sender sketch. This example only receives; it neither transmits nor saves learned data to flash.

## Prepare and run

1. Follow the [example guide](../../docs/GUIDE_EXAMPLES.md) for installation and wiring.
2. Connect power, GND and OUT of a demodulating IR receiver; match the input GPIO and inversion setting.
3. Open [04_learn.ino](04_learn.ino), select your board and upload. Open the Serial monitor at 115200 baud.

## Walk through the code

1. `rx.read(r)` supplies the captured waveform and protocol candidates. Process the result before the next read because the RAW view borrows receiver storage.
2. `r.candidate()` returns the highest-ranked candidate, or `nullptr` when none matched. `printBitsSendSnippet()` prints a compact protocol-based send snippet for that candidate.
3. When no protocol matches, `printRawSendSnippet()` prints a tick array and a RAW send call. Copy the complete generated snippet, including the array, into a sketch with a configured `IRSender`.

## Check the result

A button press produces `---- learned ----` and generated code. Capture several short presses, compare them, then test the copied code with an independent receiver.

## Adapt the example

Learning a held-button repeat alone may not capture the initial command. RAW stores demodulated mark/space durations, not the optical carrier frequency; choose the replay carrier from the remote specification or a known protocol. Use `06_ac_learn` for long AC bursts.

`IRReceiveResult::raw` is a view into receiver-owned storage. Copy ticks to your own array if they must survive another `read()` or `end()`. Where this sketch does not print the `begin()` result, check it first when reception never starts.

[Example index](../README.md) · [Library README](../../README.md)
