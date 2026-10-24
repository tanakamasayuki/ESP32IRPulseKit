# 07_ac_send — Build and transmit an AC state

> 日本語版: [README.ja.md](README.ja.md)

Construct a Panasonic AC state with named setters, change its temperature, then send a power-off state. The sequence repeats and can operate a compatible appliance.

## Prepare and run

1. Follow the [example guide](../../docs/GUIDE_EXAMPLES.md) for installation and wiring.
2. Prepare an IR LED with a suitable driver circuit and match the output GPIO to your wiring.
3. Open [07_ac_send.ino](07_ac_send.ino), select your board and upload. Open the Serial monitor at 115200 baud.

## Walk through the code

1. `IRSender tx(4)` initializes the emitter output with `tx.begin()`. The sketch leaves phase-aligned carrier enabled. When adapting it, handle initialization failure before allowing application sends.
2. A `Panasonic::Frame` is populated with power, COOL mode, AUTO fan and temperature. It represents a whole state, so sending a temperature also includes the other settings.
3. `ac::send(tx, f)` converts the frame to RAW, including its checksum, and transmits it. The loop sends temperatures from 28 through 26 degrees, then changes power to false and sends again.

## Check the result

Serial prints `cool ...C`, followed by `sent` or `send failed`, then `power off`. Test the waveform with a separate receiver first; a successful local send does not acknowledge appliance reception.

## Adapt the example

Choose a frame type matching the actual remote and preserve all intended state fields. When replacing the demonstration temperature loop, also change the repetition policy and intervals to match your application. Learn an original remote with `06_ac_learn` if the model or state layout is uncertain.

`send()` waits for local transmission to finish. A true result does not confirm reception or execution by the appliance. If false, check initialization, GPIO selection, input data and RMT resources.

[Example index](../README.md) · [Library README](../../README.md)
