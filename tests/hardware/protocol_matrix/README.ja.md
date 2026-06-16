# protocol matrix

> English: [README.md](README.md)

`protocol_matrix/` は ESP32IRPulseKit の自前TX -> 自前RXを複数protocolで確認する実機テストです。`link_smoke/` より広いprotocol範囲を見るため、リリース前確認に含めます。

pytest-embedded のpeer構成に合わせ、親sketchをRX、`peer_tx/` をTXに固定します。peer名は `tx` のままにし、ポート指定は `TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3` を使います。

## 対象

- NEC
- SONY12
- SAMSUNG32
- JVC24

## 実行

```sh
cd tests
uv run --env-file .env pytest hardware/protocol_matrix/
```

pytestは `PROTOCOL_MATRIX_OBSERVED` として、protocol、bits、score、raw_lenを出力します。scoreやraw_lenは物理条件の影響を受けるため、厳密な固定値にはしません。
