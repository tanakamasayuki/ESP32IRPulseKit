# IR dump (manual)

> Japanese: [README.ja.md](README.ja.md)

A hand-driven IR **dump tool**, not a pass/fail test. It exists so you don't have
to edit the GPIO in a receive example by hand: it reads `tests/.env` for the
serial port and RX GPIO, flashes one combined dump sketch, and prints every
received frame to the console. Point a remote at the receiver and read the dump.
Stop with **Ctrl-C**.

The sketch merges the three receive examples into one, so generic remotes and
air-conditioner remotes are handled at the same time:

- `examples/01_rx_dump` — RAW waveform + every decoded candidate, with a
  per-protocol named-field decode (NEC addr/cmd, Sony data, ...).
- `examples/06_ac_learn` — AC vendor decode (Panasonic / Gree / Mitsubishi) tried
  on the RAW capture, printed as a comment.
- `examples/04_learn` — copy-paste C++ to re-send the frame (decoded snippet plus
  a RAW replay snippet).

## What it shows

Per received frame:

```text
==== IR DUMP ====
raw.len(ticks)=68 flags=0x0
raw(us): 9000 4500 560 560 560 1690 ...
-- decoded candidates --
#0 pid=1 protocol=NEC score=100 len=32 bits=0xCB3400FF frame_type=NORMAL
  frame: NEC addr=0x0 cmd=0x34
-- AC vendor decode --
// decoded: no AC vendor matched (raw replay still works)
-- send code --
// send code (decoded):
esp32irpk::IRDecodedBits bits{};
...
// send code (raw replay):
const uint16_t ticks[] = {900, 450, 56, ...};
tx.send({ticks, 68});
```

An AC remote instead fills in the AC line, e.g.
`// decoded: Panasonic AC  power=on mode=3 temp=24C fan=2  checksum=ok`.

## Run

From `tests/`:

```sh
# default: build + upload, then stream the dump
uv run python studies/dump/dump.py

# skip flashing and connect to an already-flashed board
uv run python studies/dump/dump.py --no-flash

# snappier generic-only capture (shorter idle); raise it if one AC press
# splits into several "==== IR DUMP ====" blocks
uv run python studies/dump/dump.py --idle-us 35000
```

Flashing is the default so stale/mismatched firmware can't silently fool you.

Port and GPIO come from `tests/.env`
(`TEST_SERIAL_PORT_RX_ESP32S3`, `TEST_IR_RX_GPIO`, `TEST_IR_RX_INVERTED`) — the
same keys as `studies/link_quality`. Override per-run with `--port`, `--gpio`,
`--inverted`, `--idle-us`, `--max-symbols`, `--profile`.

## Why combined works

`01_rx_dump` decodes generic protocols from candidates; `06_ac_learn` decodes AC
from the RAW capture. The dump sketch keeps decode candidates **on** (generic)
while giving the RAW path AC-sized capacity (`IR_RX_MAX_SYMBOLS`) and a long idle
(`IR_RX_IDLE_US`, 100 ms default) so a multi-frame AC burst is captured whole and
tried against the AC vendors. The only trade-off is that a long idle merges
generic repeat frames into one capture — fine for a dump.

## Layout

```text
dump.ino          combined RX dump sketch (all output formatting lives here)
dump.py           host runner: flash from .env, then stream serial
sketch.yaml       arduino-cli profile (rx_esp32s3)
build_config.toml env -> define mapping (for pytest-embedded; dump.py passes
                  the same defines directly via --build-property)
```
