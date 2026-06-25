# ESP32IRPulseKit

> Japanese: [README.ja.md](README.ja.md)

IR remote send/receive library for ESP32 Arduino Core 3.x, built on the ESP-IDF 5.x RMT driver.

It captures RAW mark/space waveforms, decodes them into normalized bits with scored protocol candidates, and transmits using a phase-aligned carrier for stable cross-library interoperability.

## Features

- **No busy-wait.** TX and RX run on the ESP32 RMT peripheral, not a bit-banged polling loop. Capture is hardware-timestamped and transmission is non-blocking, so the CPU stays free for the rest of your sketch and the timing does not drift under interrupt load.
- **Scoring-based decode, not exact-match.** Real IR timings shift with the receiver module, distance, angle, carrier duty, and ambient light. Instead of rejecting anything outside a fixed window, each capture is scored by how far it deviates from each protocol's spec, so a dirty, out-of-spec waveform still decodes as long as its bits are unambiguous.
- **Distinguishes similar protocols.** Because the deviation is folded into the score rather than discarded, look-alike protocols on the same waveform stay as ranked candidates — you get the best match plus the runners-up and their score gap, not a single yes/no guess.
- **Three working levels:** RAW ticks (`1 tick = 10us`), normalized `IRDecodedBits`, and protocol-specific `Frame` types — drop down or stay high-level as needed.
- **Phase-aligned, symbol-encoded carrier by default,** so demodulated marks are stable frame to frame and decode cleanly in other libraries.
- **Learn-and-replay of any waveform** through the RAW path, including protocols without a dedicated decoder.
- **Air-conditioner support** as a separate `esp32irpk::ac` layer over the RAW path — whole multi-byte state frames decode/encode into named fields (Panasonic so far).

## Scope

This library targets short consumer-remote frames (NEC, Sony, AEHA, etc.) — decoding them into meaningful bits, distinguishing similar protocols, and re-sending them. Air-conditioner / heat-pump remotes, where one button sends a whole multi-byte state frame, do not fit the generic codec and are handled by a **separate `esp32irpk::ac` layer** over the RAW path (Panasonic is supported; more vendors can be added). It does not try to cover every exotic protocol — broad protocol-count coverage is a non-goal, and raw capture/replay already handles arbitrary waveforms when you only need learn-and-replay.

## Supported Protocols

| Protocol ID | Bits | Notes |
|---|---|---|
| `NEC` | 32 | Address + command + repeat frame |
| `AEHA` | variable | Kaseikyo / Panasonic family (one ID for the whole family) |
| `SONY12` / `SONY15` / `SONY20` | 12 / 15 / 20 | SIRC, 40kHz |
| `SAMSUNG32` / `SAMSUNG36` | 32 / 36 | |
| `JVC` | 16 | |
| `RC5` | 14 | Bi-phase, 36kHz |
| `RC6_M0_16` / `RC6_M6_32` | 21 / 36 | Bi-phase, 36kHz |

Any other waveform can still be captured and replayed through the RAW tick path without a dedicated decoder.

## Installation

- Arduino IDE: Library Manager → search for `ESP32IRPulseKit`.
- Manual: copy this repository into your Arduino `libraries/` directory.
- Requires the ESP32 Arduino Core 3.0 or later (ESP-IDF 5.x RMT driver).

Include the single header:

```cpp
#include <ESP32IRPulseKit.h>
```

## Quick Start

Receive and decode:

```cpp
#include <ESP32IRPulseKit.h>

esp32irpk::IRReceiver rx(32, true); // GPIO 32; most receiver modules output inverted

void setup() {
  Serial.begin(115200);
  rx.begin();
}

void loop() {
  esp32irpk::IRReceiveResult<> r;
  if (!rx.read(r)) {
    delay(1);
    return;
  }
  if (const esp32irpk::IRDecodedBits *bits = r.bits()) {
    Serial.print("protocol=");
    Serial.print((unsigned)bits->protocol_id);
    Serial.print(" bits=0x");
    Serial.println((uint32_t)bits->bits, HEX);
  }
}
```

Send NEC:

```cpp
#include <ESP32IRPulseKit.h>

esp32irpk::IRSender tx(4); // GPIO 4

void setup() {
  Serial.begin(115200);
  tx.begin();
}

void loop() {
  tx.send(esp32irpk::bits::nec(0x00ff, 0x34));
  delay(1000);
}
```

The sender registers every built-in protocol at `begin()`, so `send()` automatically uses each protocol's preferred carrier.

## Configuration

### Receiver (call before `begin()`)

| Method | Default | Notes |
|---|---|---|
| `IRReceiver<MaxCandidates>(gpio, inverted)` | `MaxCandidates = 4` | Template arg caps how many candidates can be kept |
| `setDecodeCandidates(n)` | `MaxCandidates` | `0..MaxCandidates`; `0` = RAW-only mode (no decode) |
| `setIdleThresholdUs(us)` | `30000` | RMT no-signal threshold; the larger of this and registered protocols' values is used |
| `setScoreThreshold(score)` | `0` | Candidates below this score are dropped |
| `addProtocol(spec)` / `clearProtocols()` | all built-ins | Register specs to restrict or override the decode set |

### Sender

| Method | Default | When | Notes |
|---|---|---|---|
| `setCarrierHz(hz)` | `38000` | before/after `begin()` | Explicit override, range `20000..60000`; takes precedence over protocol preference |
| `clearCarrierHz()` | — | before/after `begin()` | Remove the override; fall back to protocol/library default |
| `disableCarrier()` | carrier on | before/after `begin()` | Send solid marks with no carrier modulation |
| `setCarrierDuty(duty)` | `0.33` | before/after `begin()` | Carrier on-time fraction, `0 < duty < 1` |
| `setPhaseAlignedCarrier(enable)` | `true` | before `begin()` only | Carrier generation method — see below |
| `setTxMemBlocks(blocks)` | `1` block | before `begin()` only | RMT TX memory blocks; `0` = library default |

Carrier-frequency setters are rejected while a send is in progress. `setPhaseAlignedCarrier()` and `setTxMemBlocks()` fix the TX channel layout, so they only work before `begin()`.

**Carrier duty.** The practical range is about `0.2`–`0.5`. A higher duty can reach farther but draws more power, and at close range too high a duty can saturate the receiver and *reduce* reliability; a lower duty trades range for power saving. The optimum depends on distance, so `0.33` is the common recommendation.

**TX memory blocks.** While sending, the RMT driver refills the channel from an interrupt to supply the next symbols. If another long-running interrupt blocks that refill, the channel underruns and the transmitted waveform is corrupted. More memory blocks lengthen the interval between refills, raising tolerance to interrupt latency — this is most likely to matter on single-core ESP32-C parts with the radio (Wi-Fi/BLE) active. If you hit underruns, increase the block count, or fall back to the hardware carrier with `setPhaseAlignedCarrier(false)` (lower TX precision but far fewer symbols, so it stays stable more easily). The RMT TX memory pool is shared with other users such as addressable RGB LEDs, and each SoC has a limited number of blocks, so allocating all of them to one channel is not recommended.

### Who generates the carrier

- **Phase-aligned, symbol-encoded (default).** The RMT encoder emits each mark as an integer number of full carrier cycles starting at phase 0. Every mark holds a deterministic cycle count, so the demodulated mark (and the space after it) is stable frame to frame, which is best for decoding in other libraries. The cost is more RMT symbols per frame (roughly one per carrier cycle); `setTxMemBlocks()` raises the streaming headroom for applications under heavy ISR load (e.g. concurrent flash writes).
- **Free-running hardware carrier** (`setPhaseAlignedCarrier(false)`). The RMT peripheral overlays its own carrier (`rmt_apply_carrier`): far fewer symbols, but the phase is not reset per mark, so the demodulated mark wobbles by ±1 carrier cycle (~26µs at 38kHz). That can push short-mark protocols (JVC, AEHA) outside the tightest external-decoder windows.

See [DESIGN.md](DESIGN.md) §8 and §12 for the full carrier and timing model.

## Examples

| Example | Description |
|---|---|
| [01_rx_dump](examples/01_rx_dump) | Receive, decode, and print candidates and frames to Serial |
| [02_nec_tx](examples/02_nec_tx) | Send NEC frames |
| [03_send_protocols](examples/03_send_protocols) | Send one frame per built-in protocol |
| [04_learn](examples/04_learn) | Learn a remote: receive and print copy-paste C++ to re-send it |
| [05_raw_monitor](examples/05_raw_monitor) | RAW-only capture plus receive statistics |
| [06_ac_learn](examples/06_ac_learn) | Learn an air conditioner: RAW capture, decoded summary, and copy-paste re-send code |
| [07_ac_send](examples/07_ac_send) | Build and send Panasonic air-conditioner frames from scratch |

## Documentation

- [SPEC.md](SPEC.md) — public API contract
- [DESIGN.md](DESIGN.md) — implementation notes, scoring and carrier model
- [tests/TEST_PLAN.md](tests/TEST_PLAN.md) — test strategy

## Tests

Tests live under `tests/`.

```sh
cd tests
cp .env.example .env
# Edit .env for your local serial ports and GPIOs.
uv run --env-file .env pytest pc
uv run --env-file .env pytest hardware/link_smoke
```

`pytest pc` runs all PC tests (`fixtures`, `codec_smoke`, `compile`); `hardware/` holds the two-board pass/fail regression tests. Board investigations under `studies/` are not auto-collected (`study_*.py`). See [tests/README.md](tests/README.md) for details.

## License

MIT License. See [LICENSE](LICENSE).
