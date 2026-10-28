# RMT Coexistence Single-Board Test

> Japanese: [README.ja.md](README.ja.md)

Checks that the library's RMT channels and another RMT user can share one chip, created in any order.

- `IRReceiver`, `IRSender` (phase-aligned and modulated), and a WS2812-style 10MHz channel from the Arduino RMT API (`rmtInit()`) all `begin()` in every tested order.
- Each `IRSender` sends a short frame afterwards.

The RMT clock source is shared by every channel on the chip, and on SoCs other than ESP32 / ESP32-S2 the clock divider is shared too and picked by the first channel created. A channel with a different clock source or a too different resolution fails to begin (`group clock conflict`, `channel prescale out of the range`).

It runs on the RX board alone (the `dut`; no peer) and needs no IR wiring: the TX channels drive free pins (GPIO 25/26 on ESP32, 5/6 elsewhere) that only need to be unconnected.

## Serial Protocol

| Command | Response |
| --- | --- |
| `READY` | `COEXIST_READY` |
| `RUN` | one `CASE <name> <OK\|FAIL> <step>=<0\|1> ...` line per case, then `DONE` |

```sh
cd tests
uv run --env-file .env pytest hardware/rmt_coexist --profiles all
```
