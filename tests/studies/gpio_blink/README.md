# GPIO blink (manual)

> Japanese: [README.ja.md](README.ja.md)

A library-free check of the IR TX pin. The sketch toggles `TEST_IR_TX_GPIO` slowly with plain GPIO writes — no ESP32IRPulseKit, no RMT, no carrier — so you can watch the IR LED through a phone camera (front cameras usually pass IR) and tell a wiring/drive problem from a library problem.

- LED follows the blink → the pin and drive circuit work; if IR sending still fails, look at the library.
- LED does not follow → the problem is the pin or the drive circuit.

The pin is configured input+output, so every level change also reports what the pad actually reads (`GPIO set=1 read=1 led=on`). A `set`/`read` mismatch means the pin is not reaching the level it drives: the module's DATA input is loading it down, or it is the wrong / an input-only pin (ESP32 GPIO34-39 are rejected at boot with `GPIO_ERROR not_output_capable`).

## Run

From `tests/`:

```sh
# ESP32-S3 TX board (default)
uv run python studies/gpio_blink/blink.py

# ESP32 classic TX board
uv run python studies/gpio_blink/blink.py --peer-profile tx:tx_esp32

# hold the LED on to measure the pin with a multimeter
uv run python studies/gpio_blink/blink.py --peer-profile tx:tx_esp32 --hold on

# strongest pad drive (GPIO_DRIVE_CAP_3), in case the module input needs more current
uv run python studies/gpio_blink/blink.py --peer-profile tx:tx_esp32 --drive 3
```

Options: `--period MS` (toggle period, default 1000), `--hold on|off`, `--drive 0-3`, `--no-flash`. Stop with Ctrl-C; a summary with the readback mismatch count is printed.

The port comes from `TEST_SERIAL_PORT_PEER_TX_<PROFILE>` in `tests/.env` (the same key the two-board tests use for the TX board), and the pin from `TEST_IR_TX_GPIO` / `TEST_IR_TX_INVERTED`. With `IR_TX_INVERTED=1`, "LED on" drives the pin low.

## Serial commands

| Command | Response |
| --- | --- |
| `READY` | `GPIO_READY gpio=<n> inverted=<0|1> output_capable=<0|1> drive=<0-3>` |
| `PING` | `PONG` |
| `ON` / `OFF` | `GPIO set=<0|1> read=<0|1> led=<on|off>` (holds the level) |
| `BLINK <ms>` | `GPIO_BLINK <ms>`, then a `GPIO set=...` line per toggle |
| `DRIVE <0-3>` | `GPIO_DRIVE <n>` |
