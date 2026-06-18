# Protocol Matrix

> Japanese: [README.ja.md](README.ja.md)

`protocol_matrix/` verifies ESP32IRPulseKit TX -> ESP32IRPulseKit RX with multiple protocols on real hardware. It is broader than `link_smoke/` and is part of release verification.

The primary sketch is RX and `peer_tx/` is TX. The peer name stays fixed as `tx`, so the TX port uses `TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3`.

## Cases

- NEC
- SONY12
- SONY15
- SONY20
- SAMSUNG32
- SAMSUNG36
- JVC24
- JVC32
- AEHA
- PANASONIC40
- PANASONIC48

## Run

```sh
cd tests
uv run --env-file .env pytest hardware/protocol_matrix/
```

pytest prints `PROTOCOL_MATRIX_OBSERVED` with protocol, bits, score, and raw_len. Score and raw_len depend on the physical setup, so they are not fixed to exact values.
