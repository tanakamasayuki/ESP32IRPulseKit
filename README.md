# ESP32IRPulseKit

> Japanese: [README.ja.md](README.ja.md)

IR remote send/receive library for ESP32 Arduino Core 3.x, built on the ESP-IDF 5.x RMT driver.

It captures RAW mark/space waveforms, decodes them into normalized bits with scored protocol candidates, and transmits using a phase-aligned carrier for stable cross-library interoperability.

## Features

- RMT-based TX and RX: hardware-timed envelopes and hardware-timestamped capture, no per-edge interrupt jitter.
- Three working levels: RAW ticks (`1 tick = 10us`), normalized `IRDecodedBits`, and protocol-specific `Frame` types.
- Multi-candidate decoding: each capture is scored against registered protocols and returned best-first, so similar protocols stay distinguishable.
- Phase-aligned, symbol-encoded carrier by default, so demodulated marks are stable frame to frame and decode cleanly in other libraries.
- Learn-and-replay of any waveform through the RAW path, including protocols without a dedicated decoder.

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

## Examples

| Example | Description |
|---|---|
| [01_rx_dump](examples/01_rx_dump) | Receive, decode, and print candidates and frames to Serial |
| [02_nec_tx](examples/02_nec_tx) | Send NEC frames |
| [03_send_protocols](examples/03_send_protocols) | Send one frame per built-in protocol |
| [04_learn](examples/04_learn) | Learn a remote: receive and print copy-paste C++ to re-send it |
| [05_raw_monitor](examples/05_raw_monitor) | RAW-only capture plus receive statistics |

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
