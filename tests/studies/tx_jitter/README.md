# TX Jitter

> Japanese: [README.ja.md](README.ja.md)

Measures the transmit timing stability (jitter) of each IR library by sending a
fixed frame (NEC `0xCB3400FF`) many times and capturing every edge with a
high-resolution receiver.

The receiver is **not** the ESP32IRPulseKit library RX (which is fixed at
10 us/tick). Each variant's primary sketch drives the ESP32 RMT peripheral
directly at **1 MHz (1 us/tick)** and dumps every captured edge duration in
microseconds (`RX_JITTER seq=.. len=.. us=..`). RMT capture is hardware-timed
and very stable, so the spread measured across repeats reflects the
transmitter, not the receiver.

## Variants

```text
pulsekit/          # TX: ESP32IRPulseKit (RMT)
irremoteesp8266/   # TX: IRremoteESP8266
arduino_irremote/  # TX: Arduino-IRremote
```

Each directory keeps the 1 us RMT RX as the primary sketch and reuses the
matching `peer_tx/` transmitter from `studies/compat_matrix/`.

## Run

```sh
cd tests
uv run --env-file .env pytest -o python_files="study_*.py" studies/tx_jitter/
```

For each variant pytest sends NEC 50 times, aligns the captures by edge index
(a correct NEC always has the same edge count; only durations vary), and prints
`JITTER_OBSERVED` with per-edge statistics:

- `mean_stdev_us` — average per-edge standard deviation across repeats
- `max_stdev_us` — worst per-edge standard deviation
- `max_ptp_us` — worst per-edge peak-to-peak (max − min)

Expectation: RMT-based transmitters (ESP32IRPulseKit) should show low jitter;
timer/delay-based libraries may show larger spreads. The test only asserts that
enough consistent-length frames were captured to make the statistics
meaningful; the numbers themselves are recorded as observations.

Two details keep the capture clean and the frame counts equal across variants
(same as the loopback rig):

- **Paced serial output.** A full `RX_JITTER` line is ~400 bytes; pushed over the
  115200 USB-UART bridge in one continuous burst, the host/bridge receive buffer
  occasionally overruns and drops a few bytes mid-line. `dumpFrame()` flushes the
  line in ~16-value chunks with a short inter-chunk gap. It also **re-arms the RMT
  RX before** the paced emit (not after): the transmitter is a separate board the
  test triggers the moment it sees the line, so an unarmed window during the slow
  emit would drop the next capture.
- **First-capture priming.** The RX sketch drops its first emitted frame, and the
  test sends one warm-up frame via the peer before the measured loop, so every
  variant logs exactly `FRAMES` frames.

Analyze the raw logs with the shared `studies/tx_jitter_loopback/analyze.py`
(same `RX_JITTER` format), which also reports the `corrupt` count.

> Note: this over-the-air rig measures the IR LED + TSOP carrier demodulation as
> well as the transmitter, which dominates and can mask/invert the true TX
> ranking. For **pure** transmit jitter, use the carrier-off wired rig in
> [`studies/tx_jitter_loopback/`](../tx_jitter_loopback/README.md).
