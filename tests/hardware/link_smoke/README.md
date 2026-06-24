# Link Smoke Two-Board Test

> Japanese: [README.ja.md](README.ja.md)

The standard automated target is a two-board ESP32-S3 setup. Other SoCs such as ESP32 classic are checked first with `examples/`.

This directory uses the pytest-embedded peer layout.

- `link_smoke.ino`: RX board, exposed as `dut` in pytest
- `peer_tx/peer_tx.ino`: TX board, exposed as `peers["tx"]`

Expected wiring:

- TX GPIO -> IR LED driver
- RX GPIO <- IR receiver module
- TX/RX boards connected to the PC over USB

Configure ports and GPIOs in `.env`.

```sh
TEST_SERIAL_PORT_RX_ESP32S3=/dev/ttyUSB1
TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3=/dev/ttyUSB0
TEST_IR_TX_GPIO=4
TEST_IR_TX_INVERTED=0
TEST_IR_RX_GPIO=32
TEST_IR_RX_INVERTED=1
```

GPIO settings are environment-specific and are not hard-coded in sketches. `build_config.toml` maps `.env` `TEST_IR_*` keys to compile-time defines.

| `.env` | sketch define | Meaning |
| --- | --- | --- |
| `TEST_IR_TX_GPIO` | `IR_TX_GPIO` | TX board IR LED output GPIO |
| `TEST_IR_TX_INVERTED` | `IR_TX_INVERTED` | TX output inversion, usually `0` |
| `TEST_IR_RX_GPIO` | `IR_RX_GPIO` | RX board IR receiver module input GPIO |
| `TEST_IR_RX_INVERTED` | `IR_RX_INVERTED` | RX input inversion, commonly `1` for receiver modules |

## Serial Protocol

TX/RX print ready lines after boot.

```text
TX_READY gpio=<gpio> inverted=<0|1>
RX_READY gpio=<gpio> inverted=<0|1>
```

Common command:

| Command | Response |
| --- | --- |
| `PING` | `PONG` |

TX command:

| Command | Success response |
| --- | --- |
| `SEND NEC <address_hex> <command_hex>` | `TX_OK NEC <address_hex> <command_hex>` |
| `LOOP NEC <address_hex> <command_hex> [interval_ms]` | `TX_LOOP NEC <address_hex> <command_hex>` |
| `STOP` | `TX_STOPPED` |

`LOOP NEC` is for wiring checks. TX continuously sends while printing `TX_LOOP_SENT`. Watch the receiver module LED and RX Serial `RX_RAW` / `RX_DECODE` output to isolate the IR LED, receiver module, direction, distance, and GPIO settings. `interval_ms` is decimal and defaults to `250`.

RX output:

| Output | Meaning |
| --- | --- |
| `RX_RAW len=<n>` | RAW received with no decode candidate |
| `RX_DECODE protocol=<name> score=<score> len=<bits> bits=0x<hex> type=<NORMAL|REPEAT>` | Best decode candidate |

## pytest Flow

1. Put the RX board in receive mode.
2. Send a command such as `SEND NEC 00ff 34` to the TX board.
3. Read the RX board Serial decode output.
4. Assert protocol, bits, and score.

Run example:

```sh
cd tests
uv run --env-file .env pytest hardware/link_smoke
```

For a primitive check before the automated assertion, send this to the TX Serial port.

```text
LOOP NEC 00ff 34 250
```

To stop:

```text
STOP
```

Fixed RAW send is planned as `SEND_RAW`. `SEND protocol bits` checks the integrated sender/receiver path, while `SEND_RAW raw_ticks` is for decode behavior against known waveforms.
