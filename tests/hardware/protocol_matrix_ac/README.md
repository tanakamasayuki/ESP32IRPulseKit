# Protocol Matrix (A/C)

> Japanese: [README.ja.md](README.ja.md)

`protocol_matrix_ac/` is the air-conditioner analog of `protocol_matrix/`: it verifies ESP32IRPulseKit TX -> ESP32IRPulseKit RX for the `esp32irpk::ac` layer on real hardware, with no external library. It is part of release verification.

The peer sends each vendor's default known-good state via `ac::send`; the RX primary identifies it with `ac::decodeAny`, re-decodes the matched vendor's `Frame`, and echoes the recovered bytes. A case passes when the recovered vendor + state bytes equal what the peer reported sending, with a valid checksum. One representative state per vendor is enough for a self round-trip gate; per-field coverage (every mode/fan/temp) is exercised on the host by `pc/codec_smoke`.

Cross-implementation interop (IRremoteESP8266, HeatpumpIR) is a separate concern covered by `studies/compat_matrix_ac/`.

The primary sketch is RX and `peer_tx/` is TX. The peer name stays fixed as `tx`, so the TX port uses `TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3`. The peer transmits with the phase-aligned carrier (the library default; some vendors such as Gree and Daikin require it), so this gate does not offer a hardware-carrier mode.

## Cases (one per vendor)

- PANASONIC
- GREE
- MITSUBISHI
- FUJITSU
- DAIKIN
- TOSHIBA
- SAMSUNG
- SHARP
- KELVINATOR
- MIDEA
- CARRIER
- HITACHI
- HAIER
- MITSUBISHI_HEAVY
- TCL

## Run

```sh
cd tests
uv run --env-file .env pytest hardware/protocol_matrix_ac/
```

pytest prints `PROTOCOL_MATRIX_AC` with the vendor, round-trip ratio, and the state bytes.
