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

PulseKit's `protocol_matrix` verifies 11 protocols with in-house TX -> in-house
RX. `compat_matrix` compares that surface against external libraries, so cases
with standard external decode/send support should be added first. If an external
library does not have standard coverage for a PulseKit variant, the test may
fail, but the README should record why and preserve the observed log.

Local library coverage:

| PulseKit protocol | Arduino-IRremote | IRremoteESP8266 | compat policy |
|---|---|---|---|
| NEC | TX/RX support | TX/RX support | Normal compatibility target |
| SONY12 | TX/RX support | TX/RX support | Normal compatibility target |
| SONY15 | TX/RX support | TX/RX support | Normal compatibility target |
| SONY20 | TX/RX support | TX/RX support | Normal compatibility target |
| SAMSUNG32 | TX/RX support | TX/RX support | Normal compatibility target |
| SAMSUNG36 | Does not support Samsung36 | `sendSamsung36()` / `decodeSamsung36()` exist | Normal compatibility target for IRremoteESP8266. Arduino-IRremote does not support Samsung36, so it is out of scope |
| JVC | TX/RX support (16-bit) | TX/RX support (16-bit) | Normal compatibility target |
| AEHA | Kaseikyo family exists | Panasonic/Kaseikyo family exists | Canonical decoder for AEHA/Kaseikyo/Panasonic; IRremoteESP8266 cross-test: tx works, rx marginal (mark width, see below), self baseline |
| RC5 | TX/RX support | TX/RX support | IRremoteESP8266 cross-test (rx + tx) |
| RC6_M0_16 | RC6 family support | RC6 family support | IRremoteESP8266 cross-test (rx + tx) |
| RC6_M6_32 | RC6A/RC6 family support | RC6 family support | Investigation candidate; watch mode-6A representation |

Priority:

1. `RC5` and `RC6_M0_16`: in the IRremoteESP8266 cross-tests (rx + tx). See
   "RC5 / RC6 biphase convention" below.
2. `AEHA` (Kaseikyo/Panasonic): in the IRremoteESP8266 cross-tests (rx + tx + self).
   See "AEHA / Kaseikyo / Panasonic" below.
3. Investigate `RC6_M6_32` before adding it as a required compatibility case.

### RC5 / RC6 biphase convention

RC5 and RC6 are biphase (Manchester) and use **opposite** half-bit polarity:

- **RC5**: a `1` is space→mark, a `0` is mark→space. The leading idle space of the
  first start bit is not part of the captured RAW (which begins on the first mark).
- **RC6**: a `1` is mark→space, a `0` is space→mark, after a `2666 / 889 µs` leader.
  The start bit is single-width; only the toggle (4th) bit is double-width.

The integer representations still differ from IRremoteESP8266 (PulseKit counts the
start/mode/toggle bits — 14 bits for RC5, 21 for RC6 mode 0 — while IRremoteESP8266
reports 12-13 / 20 with those stripped), so the cross-test records `bit_order`
rather than asserting a value match.

### AEHA / Kaseikyo / Panasonic

`AEHA` is the canonical decoder for the AEHA (家製協) family. Kaseikyo — and
Panasonic, which is Kaseikyo with a specific manufacturer code — is a 48-bit AEHA
frame, so an IRremoteESP8266 Panasonic frame decodes on PulseKit as `AEHA`
(e.g. `0xBD3D802002`). PulseKit's AEHA decoder validates the customer-code parity
nibble, which is what distinguishes a real家製協 frame.

Bit order: PulseKit stores LSB-first, IRremoteESP8266 MSB-first, so the same 48-bit
waveform reads bit-reversed between them (the Panasonic manufacturer code is
`0x2002` in PulseKit's low 16 bits and `0x4004` in IRremoteESP8266's high 16 bits).
The cross-tests use the same physical frame from both ends, named per each library's
own representation: tx/self send the IRremoteESP8266 Panasonic value `0x40040100BCBD`
(via `sendPanasonic64`), and rx sends its PulseKit-native form `0xBD3D802002` (AEHA),
which is exactly what PulseKit decodes that waveform as.

Observed results:

- **tx (IRremoteESP8266 -> PulseKit): works.** PulseKit decodes the Panasonic frame
  as `AEHA len=48 bits=0xBD3D802002` (score ~800).
- **rx (PulseKit -> IRremoteESP8266): marginal / does not decode.** PulseKit emits a
  correct AEHA waveform (IRremoteESP8266 captures it as RX_RAW), but it is not
  decoded as Panasonic. Root cause is mark width, not bit order or checksum
  (`decodePanasonic` runs non-strict, so manufacturer/checksum are not checked):
  AEHA's spec mark is 425 µs, and PulseKit's RMT free-running carrier truncates it
  so the receiver measures ~385 µs on average with ~5 % of marks below ~348 µs.
  IRremoteESP8266's Panasonic decoder expects marks near `432 + 50 µs` excess and
  rejects anything below ~362 µs; a 48-bit frame needs every mark in range, so it
  almost always fails. This is the carrier-phase mark-width limitation from
  DESIGN §12 surfacing on another short-mark protocol. We do not shorten AEHA's
  spec mark for interop, so this stays a recorded incompatibility (NEC at 560 µs
  has a wide enough window and is unaffected). Confirmed fixable: with the
  experimental phase-aligned carrier (`PULSEKIT_CARRIER=pa`, see
  `studies/phase_aligned_carrier`), the rx peer's marks stay in range and
  IRremoteESP8266 decodes the frame as `PANASONIC48` (hw = 0/5, pa = pass).
- **self (IRremoteESP8266 -> IRremoteESP8266): baseline**, environment permitting
  (the 48-bit frame is the most placement-sensitive case in the matrix).

### SAMSUNG36 (two-block)

The Samsung36 format is a **two-block** waveform: header, the top 16 bits, an
inter-block separator space, then the low 20 bits, sent **MSB-first**, with bit
timings `512 / 1468 / 490 µs` and header `4515 / 4438 µs`. PulseKit follows that
format, encoded/decoded via a protocol-specific path (`encodeSamsung36` /
`decodeSamsung36`), the same way RC5/RC6 use dedicated paths. The value is stored
MSB-first (`bits[35..20]` = address/block 1, `bits[19..0]` = command/block 2).
Against an external library that also implements Samsung36 (IRremoteESP8266) the
cross-test observes `bit_order = same`. Arduino-IRremote does not support Samsung36,
so it is not part of this cross-test.

### Arduino-IRremote self-test: known baseline reds

`arduino_irremote_self` (Arduino-IRremote TX → Arduino-IRremote RX, PulseKit not
involved) leaves a few cases red. These reflect Arduino-IRremote's own decoder, not
PulseKit interop:

- **SONY12 / SONY15 / SONY20**: the RX receives the frame cleanly (a well-formed
  `RX_RAW` is dumped) but Arduino-IRremote's Sony decoder rejects it. The TSOP demod
  inflates the 600 µs SIRC space toward ~800 µs, past that decoder's space tolerance.
  Both cross directions decode Sony (our TX → Arduino RX, Arduino TX → our RX), so
  PulseKit's Sony is interoperable; only Arduino-IRremote decoding its own transmitter
  fails here. The inflation scales with placement, so this case **can pass at a
  favorable distance/alignment** — treat the red as environment-dependent, not fixed.
- **SAMSUNG36**: Arduino-IRremote does not implement the two-block Samsung36 form, so
  it cannot decode the frame (same reason it is out of scope for the cross-test).

NEC self is green: identical frames sent within Arduino-IRremote's ~110 ms NEC repeat
window are flagged as repeats (`OTHER_8` / `REPEAT`) and discarded by the harness, so
the studies space trials by `INTER_TRIAL_GAP_S` to keep each send an independent frame.

## Current findings (NEC)

Running the NEC case at a **very short TX↔RX distance (<10 cm)** gives 2 of 4
directions failing, while the other 2 pass:

| Direction | Result | What the RX sees |
|---|---|---|
| IRremoteESP8266 (50% duty) TX → our RX | ✅ pass | zero-space ~590 us |
| our TX → Arduino-IRremote RX | ✅ pass | — |
| **Arduino-IRremote (30% duty) TX → our RX** | ❌ fail | zero-space inflates to **~780 us** (> our 700 us ceiling) |
| **our TX → IRremoteESP8266 RX** | ❌ fail | zero-space ~594–672 us (> their ~637 us ceiling) |

### The failures are a close-range TSOP demodulation **bias**, not jitter

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

Distance is the only lever that can fix Failure B (we cannot change
IRremoteESP8266's decoder); at a realistic distance the TSOP behaves normally and
both failing directions decode. `studies/link_quality/` (manual meter) finds a
placement where the zero-space bias is small and the external-RX "compat margin"
is positive.

### JVC → IRremoteESP8266: root cause isolated

The JVC case (PulseKit TX → IRremoteESP8266 RX) is marginal (~1/5) even though
`irremoteesp8266_self` decodes JVC 5/5 at the same placement — so it is a
**transmitter** issue, not environment. Raising TX duty to 50 % does not fix it.
The `studies/carrier_loopback/` probe (1 µs RMT capture of the raw carrier, no
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
