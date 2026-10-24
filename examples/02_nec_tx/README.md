# 02_nec_tx — Send an NEC command

> 日本語版: [README.ja.md](README.ja.md)

Build an NEC command from an address and command byte and send it repeatedly through an IR LED driver.

## Prepare and run

1. Follow the [example guide](../../docs/GUIDE_EXAMPLES.md) for installation and wiring.
2. Prepare an IR LED with a suitable driver circuit and match the output GPIO to your wiring.
3. Open [02_nec_tx.ino](02_nec_tx.ino), select your board and upload. Open the Serial monitor at 115200 baud.

## Walk through the code

1. `IRSender tx(4)` selects the output GPIO, not an LED power supply. `tx.begin()` allocates and starts the RMT transmitter; check its result before relying on sends.
2. `bits::nec(0x00ff, 0x34)` creates an `IRDecodedBits` value. Change the address and command together to values learned from your remote, rather than guessing from the hexadecimal appearance.
3. `tx.send(bits)` encodes and transmits that value. The commented `NECFrame` alternative gives named fields and converts them with `toBits()`; use either representation.

## Check the result

The Serial monitor prints `sent` or `send failed` once per loop. A second board running `01_rx_dump` can verify the emitted protocol and fields without depending on appliance behavior.

## Adapt the example

The delay spaces repeated commands; this sketch continues sending until stopped. Leave automatic protocol carrier selection in place unless you know the required carrier. An explicit `setCarrierHz()` overrides that selection.

`send()` waits for local transmission to finish. A true result does not confirm reception or execution by the appliance. If false, check initialization, GPIO selection, input data and RMT resources.

[Example index](../README.md) · [Library README](../../README.md)
