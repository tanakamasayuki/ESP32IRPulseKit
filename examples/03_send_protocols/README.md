# 03_send_protocols — Compare protocol-specific send helpers

> 日本語版: [README.ja.md](README.ja.md)

Send a sequence of different built-in remote protocols and report the result of each transmission.

## Prepare and run

1. Follow the [example guide](../../docs/GUIDE_EXAMPLES.md) for installation and wiring.
2. Prepare an IR LED with a suitable driver circuit and match the output GPIO to your wiring.
3. Open [03_send_protocols.ino](03_send_protocols.ino), select your board and upload. Open the Serial monitor at 115200 baud.

## Walk through the code

1. `tx.begin()` installs the built-in protocol definitions. Each `bits::...()` helper creates the protocol ID, bit length and packed data needed by `send()`.
2. `report(label, tx.send(...))` prints a local send result for each protocol. Calls run in source order; the delay at the end separates whole batches, not every individual command.
3. Carrier selection follows the protocol definition unless explicitly overridden. For named address/command fields, use the corresponding `frames::...` type and `toBits()` instead.

## Check the result

Compare each printed label with a separate receiver. RAW capture is useful if the receiver recognizes only part of the sequence; not every appliance understands every protocol.

## Adapt the example

For an application, retain only the helper matching the intended remote and replace its demonstration fields. Check bit ordering and field definitions in the protocol headers instead of treating every helper argument as the same address/command format.

`send()` waits for local transmission to finish. A true result does not confirm reception or execution by the appliance. If false, check initialization, GPIO selection, input data and RMT resources.

[Example index](../README.md) · [Library README](../../README.md)
