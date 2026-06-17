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

Each variant sends NEC 50 times and prints `JITTER_LOOPBACK_OBSERVED` with
per-edge `mean_stdev_us` / `max_stdev_us` / `max_ptp_us`. Because the IR link and
carrier are removed, differences here reflect the transmitter's raw timing
precision (RMT hardware vs timer/delay generation).
