# RX Capacity Two-Board Test

> Japanese: [README.ja.md](README.ja.md)

Checks the RX capture capacity set by `setMaxRxSymbols()` on real hardware.

- A frame of at most `cap` symbols arrives intact and is not flagged truncated.
- A longer frame is cut at `cap` symbols and `RAW_TRUNCATED` is set.

The peer sends a synthetic frame of N marks (A/C-like timing: 450us marks, alternating 450us/1300us spaces). The receiver captures it as N RMT symbols, which is `2N-1` ticks when intact and `2*cap` ticks when cut.

Wiring, `.env`, and the pytest-embedded peer layout are the same as [`link_smoke/`](../link_smoke/README.md).

## Serial Protocol

| Board | Command | Response |
| --- | --- | --- |
| both | `PING` | `PONG` |
| both | `READY` | `RX_READY ...` / `TX_READY ...` |
| RX | `CAP <symbols>` | `RX_CAP <symbols>` (re-opens the receiver; `0` = library default, 256) |
| TX | `SEND PULSES <n>` | `TX_OK PULSES <n>` |

RX prints `RX_RAW len=<ticks> truncated=<0|1>` for each capture (RAW-only mode).

## SoC coverage

The standard rig is two ESP32-S3 boards, where the capture streams through RMT RX ping-pong. ESP32 and ESP32-S2 have no ping-pong, so the frame must fit in the RX channel's RMT hardware memory; running this test with an ESP32 classic RX board covers that path (expect frames over 256 symbols to be truncated there even with a larger `cap`).

```sh
cd tests
uv run --env-file .env pytest hardware/rx_capacity
```
