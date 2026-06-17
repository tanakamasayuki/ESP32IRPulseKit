# TX Jitter (wired loopback, carrier off)

> Japanese: [README.ja.md](README.ja.md)

Measures **pure transmit timing jitter** of each IR library, isolated from the
IR link. Unlike `hardware/tx_jitter/` (over-the-air, through an IR LED + TSOP
receiver module, which adds carrier/demodulation distortion), this rig:

- Runs on **one board**. TX and the 1 us RMT receiver share the same clock.
- **Disables the carrier** (solid marks), so no 38 kHz modulation appears on the
  wire and no TSOP demodulation is involved.
- Uses a **direct wire**: connect the loopback TX pin to the loopback RX pin on
  the board (common ground is automatic — same board). The pins come from `.env`
  keys `TEST_LOOPBACK_TX_GPIO` / `TEST_LOOPBACK_RX_GPIO` (default GPIO5 → GPIO6),
  distinct from the `TEST_IR_*` keys the other rigs use.

Each variant's sketch both transmits NEC and captures the looped signal at
1 us resolution, printing `RX_JITTER seq=.. len=.. us=..`.

## Variants

```text
pulsekit/          # TX: ESP32IRPulseKit (RMT), carrier off via disableCarrier()
irremoteesp8266/   # TX: IRremoteESP8266, carrier off via use_modulation=false
arduino_irremote/  # TX: Arduino-IRremote, carrier off via USE_NO_SEND_PWM
```

Carrier-off support was added to ESP32IRPulseKit as `IRSender::disableCarrier()`.

## Wiring

Single board: jumper the loopback TX pin to the loopback RX pin (default
**GPIO5 → GPIO6**, set by `TEST_LOOPBACK_TX_GPIO` / `TEST_LOOPBACK_RX_GPIO` in
`.env`). No IR LED, no receiver module.

## Run

```sh
cd tests
uv run --env-file .env pytest hardware/tx_jitter_loopback/
```

Each variant sends NEC 50 times (override with `JITTER_FRAMES`) with a small
pause between frames (`JITTER_GAP_MS`, default 5 ms) so the serial pipeline
drains and `RX_JITTER` lines stay clean, then prints `JITTER_LOOPBACK_OBSERVED`
with per-edge `mean_stdev_us` / `max_stdev_us` / `max_ptp_us`. The per-frame
timing is highly repeatable, so a modest count is enough. Because the IR link and
carrier are removed, differences here reflect the transmitter's raw timing
precision (RMT hardware DMA vs software bit-banging).

## Analyze

`pytest` prints a one-line `JITTER_LOOPBACK_OBSERVED` summary per variant (only
visible with `-rA` / `-s`, or in the JUnit/HTML report via `record_property`).
For the full per-edge breakdown, run `analyze.py` against the captured logs. It
recomputes statistics from the raw `RX_JITTER` lines in each `dut.log`:

```sh
# Auto-detect the newest pytest-embedded run and analyze every *jitter* log
uv run python hardware/tx_jitter_loopback/analyze.py

# Or point it at specific result dirs / dut.log files, and list more edges
uv run python hardware/tx_jitter_loopback/analyze.py --worst 10 \
    /tmp/pytest-embedded/<run>/test_arduino_irremote_loopback_jitter
```

Output is a comparison table plus the worst edges per transmitter:

```text
capture                                clean corrupt  used edges  mean_sd  max_sd  max_ptp
test_arduino_irremote_loopback_jitter    500       0   500    67     0.xx    2.xx        x
test_irremoteesp8266_loopback_jitter     500       0   500    67     0.xx    2.xx        x
test_pulsekit_loopback_jitter            500       0   500    67     0.00    0.00        0
```

Reading the numbers:

- `mean_sd` — average per-edge standard deviation (typical jitter).
- `max_sd` / `max_ptp` — worst single edge; large values are interrupt-preemption
  spikes (look at the listed worst edge index/mean to see which pulse).
- `0.00` everywhere — perfectly deterministic timing (RMT hardware DMA).
- `corrupt` — lines whose declared `len=` did not match the actual count of `us=`
  values, i.e. the long serial line lost bytes in transit. These are a reporting
  artifact (not transmit jitter) and are discarded; the sketches emit each line
  in one `write()` + `flush()` to keep this near zero.
- `used` — clean frames whose edge count matched the modal length (used for the
  per-edge stats).

The same script also works on `hardware/tx_jitter/` logs (same `RX_JITTER`
format), so you can compare wired-loopback vs over-the-air runs.

## Observed results

Representative run (NEC, single-board carrier-off loopback, two ESP32-S3, no
system load). Absolute numbers depend on the setup; the relative picture is the
point.

| TX library | generation | mean_sd | max_sd | max_ptp |
| --- | --- | --: | --: | --: |
| ESP32IRPulseKit | RMT (hardware DMA) | **0.00 µs** | 0.00 | 0 |
| IRremoteESP8266 | software `delayMicroseconds` | ~0.2-0.4 µs | ~1.4 | ~9 |
| Arduino-IRremote | software `delayMicroseconds` | ~0.6-0.8 µs | ~2.4 | ~9 |

Findings:

- **RMT (ESP32IRPulseKit) is perfectly deterministic**: every edge of every
  frame is identical (0 µs jitter). The durations are quantized to the library's
  10 µs RMT tick, but perfectly repeatable.
- **Software/`delayMicroseconds` transmitters jitter at the sub-microsecond
  level** in steady state, but are vulnerable to interrupt preemption. An early
  50-frame run caught Arduino-IRremote with a single edge spiking to
  `max_ptp ≈ 800 µs` (`sd ≈ 114 µs`) — a one-off ISR preemption. With no system
  load such spikes are rare; under WiFi/BLE/other ISRs they would be more
  frequent, while RMT stays at 0 regardless.
- All values are tiny relative to the protocol tolerances (NEC ±25 % of ~560 µs),
  so all three decode fine on a clean link. The advantage of RMT is robustness
  under load, not steady-state accuracy.

Methodology note: the over-the-air rig (`hardware/tx_jitter/`) measured *larger*
spreads (single-digit to ~18 µs) dominated by the IR LED + TSOP carrier
demodulation, which masked and even inverted the true TX ranking. Removing the
carrier and TSOP (this rig) is what exposes the real per-transmitter timing.
