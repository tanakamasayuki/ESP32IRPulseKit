# Hardware Compat Matrix

> Japanese: [README.ja.md](README.ja.md)

`compat_matrix/` is for compatibility and difference investigation against external libraries or alternate implementations. Multi-protocol ESP32IRPulseKit TX -> ESP32IRPulseKit RX checks live in `hardware/protocol_matrix/`.

Each test directory keeps the primary sketch as RX and `peer_tx/` as TX. Keeping the peer name fixed as `tx` lets variants reuse `TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3`.

Variants:

```text
irremoteesp8266_tx/     # RX: ESP32IRPulseKit, TX: IRremoteESP8266
irremoteesp8266_rx/     # RX: IRremoteESP8266, TX: ESP32IRPulseKit
arduino_irremote_tx/    # RX: ESP32IRPulseKit, TX: Arduino-IRremote
arduino_irremote_rx/    # RX: Arduino-IRremote, TX: ESP32IRPulseKit
```

All four variants follow the same structure. `*_tx` variants keep the
ESP32IRPulseKit RX sketch and swap the `peer_tx/` transmitter to an external
library; `*_rx` variants keep the ESP32IRPulseKit `peer_tx/` transmitter and
swap the primary receiver to an external library. Each test records
`bit_order` (same / reversed / other) alongside the observed bits, since
implementations often differ only by MSB/LSB-first integer representation.

`compat_matrix` is optional. Use it to observe score, raw_len, decode results, raw timing variation, and bit-order/field interpretation differences.

## Protocol Coverage Policy

PulseKit's `protocol_matrix` verifies 14 protocols with in-house TX -> in-house
RX. `compat_matrix` compares that surface against external libraries, so cases
with standard external decode/send support should be added first. If an external
library does not have standard coverage for a PulseKit variant, the test may
fail, but the README should record why and preserve the observed log.

Local library coverage as of 2026-06-18:

| PulseKit protocol | Arduino-IRremote 4.7.1 | IRremoteESP8266 2.9.0 | compat policy |
|---|---|---|---|
| NEC | TX/RX support | TX/RX support | Normal compatibility target |
| SONY12 | TX/RX support | TX/RX support | Normal compatibility target |
| SONY15 | TX/RX support | TX/RX support | Normal compatibility target |
| SONY20 | TX/RX support | TX/RX support | Normal compatibility target |
| SAMSUNG32 | TX/RX support | TX/RX support | Normal compatibility target |
| SAMSUNG36 | No dedicated decoder; raw sender can emit 36 bits | `sendSamsung36()` / `decodeSamsung36()` exist | IRremoteESP8266 compatibility is a fix candidate. Arduino-IRremote RX is outside coverage |
| JVC | TX/RX support (16-bit) | TX/RX support (16-bit) | Normal compatibility target |
| AEHA | Kaseikyo family exists; relation to PulseKit AEHA not settled | Panasonic/Kaseikyo family exists; relation to PulseKit AEHA not settled | Investigation candidate |
| PANASONIC40 | Kaseikyo/Panasonic family exists; 40-bit shape not confirmed | Panasonic/Kaseikyo family exists; 40-bit shape not confirmed | Investigation candidate |
| PANASONIC48 | Kaseikyo/Panasonic family exists | Panasonic/Kaseikyo family exists | Next addition candidate |
| RC5 | TX/RX support | TX/RX support | Next addition candidate |
| RC6_M0_16 | RC6 family support | RC6 family support | Next addition candidate; watch bit representation |
| RC6_M6_32 | RC6A/RC6 family support | RC6 family support | Investigation candidate; watch mode-6A representation |

Priority:

1. Decide whether PulseKit `SAMSUNG36` should match IRremoteESP8266's two-block Samsung36 waveform.
2. Add `PANASONIC48`, `RC5`, and `RC6_M0_16` after confirming external API value representation.
3. Investigate `AEHA`, `PANASONIC40`, and `RC6_M6_32` before adding them as required compatibility cases.

## Current findings & hypotheses (NEC, as of 2026-06-18)

Running the NEC case at a **very short TX↔RX distance (<10 cm)** gives 2 of 4
directions failing, while the other 2 pass:

| Direction | Result | What the RX sees |
|---|---|---|
| IRremoteESP8266 (50% duty) TX → our RX | ✅ pass | zero-space ~590 us |
| our TX → Arduino-IRremote RX | ✅ pass | — |
| **Arduino-IRremote (30% duty) TX → our RX** | ❌ fail | zero-space inflates to **~780 us** (> our 700 us ceiling) |
| **our TX → IRremoteESP8266 RX** | ❌ fail | zero-space ~594–672 us (> their ~637 us ceiling) |

### Hypothesis: the failures are a close-range TSOP demodulation **bias**, not jitter

At short range the TSOP demodulator **saturates** and cuts the mark's trailing
edge early; by conservation, the time lost from the mark is added to the
following space. The result is a **systematic offset**: marks arrive ~80–90 us
*short* and zero-spaces ~80–90 us *long*. A normal-distance TSOP does the
opposite (marks long, spaces short) — which is exactly what every library's
~50 us "mark excess" compensation assumes, so the close-range saturation biases
in the wrong direction and pushes the zero-space past the decode tolerance.

- **Failure A** (our RX): Arduino's 30%-duty signal inflates the zero-space to
  ~780 us, past our 560 ±25 % = 700 us window. Our decoder requires a space
  within ±25 % of *either* 560 or 1690, so 780 lands in the dead-zone.
- **Failure B** (IRremoteESP8266 RX): it *subtracts* 50 us from the desired
  space (assuming the normal direction), tightening the zero-space ceiling to
  ~637 us — the wrong way for our inflated ~650/672 us spaces. Raising our TX
  duty to 50 % did **not** fix it (still spikes to 672).

### Why the jitter rigs showed almost no difference

`tx_jitter_loopback`, `tx_jitter`, and `carrier_jitter` measured frame-to-frame
**variation** (standard deviation, ~5–30 us). This bias is a different axis:

- The loopback rig is **wired (no TSOP)**, so it has no demodulation bias at all
  — only the RMT's intrinsic determinism.
- The carrier/jitter rigs reported **sd**, normalizing out the constant mean
  offset; their headline result was "sd valleys at integer carrier cycles".
- The variation here is still ~20 us (same ballpark) — what breaks decoding is
  the ~90 us **mean bias**, which only the over-the-air *decode-compatibility*
  path surfaces.

### Status / next steps

- This is a hypothesis pending **re-measurement at a realistic distance**, where
  the TSOP should behave normally and both failing directions are expected to
  decode. Distance is the only lever that can fix Failure B (we cannot change
  IRremoteESP8266's decoder).
- Use `hardware/link_quality/` (manual meter) to find a placement where the
  zero-space bias is small and the external-RX "compat margin" is positive.
- A possible library-side robustness improvement (fixes Failure A only): classify
  NEC bits by nearest expected space (threshold ≈1125 us) with a loose mark
  check, instead of strict ±25 % membership — how most NEC decoders work. Not
  yet implemented; it does not touch the committed 33 % default-duty decision.

### JVC → IRremoteESP8266: root cause isolated

The JVC case (PulseKit TX → IRremoteESP8266 RX) is marginal (~1/5) even though
`irremoteesp8266_self` decodes JVC 5/5 at the same placement — so it is a
**transmitter** issue, not environment. Raising TX duty to 50 % does not fix it.
The `hardware/carrier_loopback/` probe (1 µs RMT capture of the raw carrier, no
TSOP) pinned it down: the carrier period is clean, but each identical 530 µs mark
holds **20 or 21 carrier cycles in a ~50/50 coin-flip** (free-running carrier
phase). That ±1 cycle (~26 µs) shifts the demodulated mark/space enough that some
zero-spaces cross IRremoteESP8266's tight ~594 µs JVC window. See
[carrier_loopback/README.md](../carrier_loopback/README.md) for the data and the
candidate fix (per-mark carrier phase alignment, likely via a symbol-encoded
carrier at ~1 µs resolution).

**Adopted mitigation:** the library now emits a detuned **480 µs JVC zero-space**
(spec is 525 µs) to move the received zero-space clear of the 594 µs window — a
transmit-margin workaround, not a root fix. Validated on hardware: our TX → 
IRremoteESP8266 JVC now decodes (was ~1/5), Arduino-IRremote stays 15/15
([jvc_verify_arduino/](../jvc_verify_arduino/)), and our RX still decodes external
standard 525 µs JVC (score ~920). See the note in `src/protocols/JVC.h`.
