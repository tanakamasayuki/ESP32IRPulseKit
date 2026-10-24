# Running and adapting the IR examples

> 日本語版: [GUIDE_EXAMPLES.ja.md](GUIDE_EXAMPLES.ja.md)

Use this guide with the [example index](../examples/README.md). Each example has its own README for the exact operations and output. Installation requirements and supported protocols live in the [library README](../README.md); check that page when selecting your board and core.

## Prepare the hardware

For reception, use a demodulating IR remote receiver rather than a bare photodiode. Check the module datasheet for VCC, GND and OUT: pin order and permitted supply voltage differ between modules. Its OUT level must be suitable for the ESP32 input. Connect the grounds, select an available input GPIO, and update `IRReceiver rx(pin, inverted)`. The examples use an inverted input for a typical active-low receiver; change it if the actual module has the opposite polarity. The example GPIO numbers are wiring choices, not universal board pins.

For transmission, use an IR LED with current limiting and a driver circuit appropriate to the LED and supply. `IRSender tx(pin)` selects the signal pin controlling that circuit; it does not regulate LED current. Check the circuit polarity and connect its ground reference to the board. Do not infer a module's electrical pinout from its connector shape or the example GPIO number.

Install the library, open the chosen `.ino`, select the actual board and port, upload, then open the Serial monitor at the sketch's baud rate. Start with one receiver and a known working remote. Add a transmitter only after reception works; a second board running a receiver example is useful for checking emitted signals.

## Understand the three representations

- RAW is an alternating mark/space duration array. `IRRawTickView::len` counts entries, not bytes; each tick is 10 microseconds. `printRawMicros()` converts the values for readable output. A demodulating receiver captures the envelope, so RAW does not measure the optical carrier frequency.
- `IRDecodedBits` adds a protocol ID, frame type, bit length and packed bits. Protocol helpers construct it without manually reproducing bit ordering and redundant fields. A repeat frame can differ from the initial command.
- Protocol `Frame` types expose named fields. AC frames live in the separate `ac` layer and often carry an entire appliance state, not just the last button pressed.

## Receive, inspect, then retain data

Configure `IRReceiver` before `begin()`. Check the bool returned by initialization when bringing up new hardware. In `loop()`, call `read(result)` repeatedly; false means there is no result to process, including when the receiver was never started. Do not use stale result contents after a false return.

A true read and a successful protocol decode are separate facts. Inspect `count` or `candidate()` before accessing a candidate. Candidates are ordered by score, and a higher score does not prove the original protocol. Inspect RAW and repeat behavior when two decoders give similar results. `setDecodeCandidates(0)` deliberately disables the generic decode; AC examples instead feed the RAW data into AC decoders.

`result.raw` borrows the receiver's storage. Finish printing, decoding or copying it before another `read()` or `end()` can replace that storage. Copying the view object only copies a pointer and length; retain the tick values in your own buffer when saving a learned command.

`truncated()` and `rmtOverflow()` describe the capture. `stats()` also exposes cumulative queue drops. An incomplete waveform is unsuitable as a trusted replay source. Long captures need both enough symbol capacity and an idle threshold longer than the signal's internal gaps. Apply capacity settings before initialization and check configuration return values. Avoid hard-coding assumptions about a chip's capture limits; consult the API and hardware constraints.

## Transmit and interpret the result

Configure an `IRSender`, check `begin()`, construct decoded bits or a RAW view, then check `send()`. Hardware generates the waveform, but **the public `send()` call waits for local transmission to finish**. Account for that time in your loop, especially for long AC frames and repeats. It is not a completion callback API.

A true result confirms local send completion, not reception by the appliance. RAW replay needs the correct carrier as well as complete durations. Sending decoded bits selects the protocol's carrier unless explicitly overridden with `setCarrierHz()`. Keep the default phase-aligned carrier for the AC examples; see the root configuration reference before changing carrier mode or allocating more RMT resources.

The repeat argument means additional transmissions; the default negative value selects the protocol default for decoded bits, while RAW has no protocol from which to infer repeat behavior. Spacing and initial-versus-repeat frames matter: repeatedly sending an arbitrary captured repeat is not necessarily equivalent to pressing a button.

## Move generated learning code into an application

`04_learn` and `06_ac_learn` print code; they do not write learned data to flash and do not transmit. Copy the complete generated array/state and send expression into a sender sketch, configure its output pin and carrier, initialize it, then compare reception with the original remote. Verify one short press first.

AC learning can print both an editable setter template and preserved state bytes. Setters are convenient when changing temperature or mode but may omit unknown or unsupported fields. Preserved decoded state bytes retain those bytes; RAW keeps the captured timing envelope. None of these automatically confirms compatibility with every model sold under the same brand.

## Troubleshoot step by step

| Symptom | What to check |
|---|---|
| No receive output | Initialization result, OUT GPIO, module pinout/supply, ground and input polarity; try a known remote |
| RAW appears but no candidates | Unknown protocol, noise or repeat frame; use RAW monitor or AC learner as appropriate |
| One AC press yields several captures | Idle threshold may be shorter than an internal gap; compare multiple presses |
| `TRUNCATED` or rising overflow counts | Capture capacity, processing/printing time and RMT resources; capture again after correcting the cause |
| `send()` returns false | Transmitter initialization, arguments, selected GPIO and resource/configuration failures |
| `sent` but no appliance response | Protocol/model, state fields, carrier, complete waveform, LED driver polarity, orientation and distance |

Keep version/support tables in the root README and hardware details in the relevant board/module documentation. The walkthroughs here focus on API usage and observable behavior that you can reuse in your own sketch.
