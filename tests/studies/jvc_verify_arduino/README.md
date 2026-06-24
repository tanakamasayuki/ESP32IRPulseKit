# JVC tuned-timing cross-check (Arduino-IRremote RX)

Overfit check for the [../jvc_timing_sweep/](../jvc_timing_sweep/) winner. The
sweep tuned JVC mark/zero-space to IRremoteESP8266; this sends the same tunable
JVC (`JVCRAW` peer, identical to jvc_timing_sweep) to a **second, independent
receiver — Arduino-IRremote** — and compares the JVC standard against the tuned
candidate, to confirm the tuning is not overfit to one decoder.

- **RX (jvc_verify_arduino.ino)**: Arduino-IRremote (`IrReceiver`), prints
  `RX_DECODE protocol=JVC<bits>` on success.
- **TX (peer_tx/)**: ESP32IRPulseKit `JVCRAW <mark> <zspace> <ospace> <hex>`.

```sh
cd tests
uv run --env-file .env pytest -s -o python_files="study_*.py" studies/jvc_verify_arduino/
```

## Observed results

N=15/point, one-space 1575, bits 0xC0DE, two-board IR.

| point | mark/zspace | IRremoteESP8266 (sweep) | Arduino-IRremote |
|---|---|---|---|
| std (JVC default) | 530/530 | 0.33 | **15/15** |
| tuned (winner) | 540/480 | ~1.00 | **15/15** |
| tuned-lo | 540/470 | ~1.00 | 15/15 |
| low-mark (control) | 520/530 | ~0.17 | 15/15 |

Arduino-IRremote is tolerant and decodes **every** point 100 % (as `JVC16`). So
the tuned timing **fixes the strict receiver (IRremoteESP8266: 0.33 → ~1.00) with
no regression on Arduino-IRremote**, and ESP32IRPulseKit's own RX (700 µs
zero-space window) decodes both as well — i.e. the tuning is **not overfit**.

Caveats: Arduino's wide tolerance means this confirms "stays in window", not a
hard stress test; a real JVC device can't be tested here. And tuned works by
adding *margin*, not by removing the free-running-carrier jitter
([../carrier_loopback/](../carrier_loopback/)) — so it is a pragmatic mitigation,
and adopting non-standard JVC timing (vs the 525/525 spec) is a deliberate choice
to document if taken.
