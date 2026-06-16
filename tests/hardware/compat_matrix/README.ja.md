# hardware compat matrix

> English: [README.md](README.md)

`compat_matrix/` は実機を使った互換性・差分調査用のテストです。`hardware/link_smoke/` はリリース判定用の安定smokeとして維持し、こちらはprotocol差分、bit order、raw timingのばらつき、外部ライブラリ比較を調べるために使います。

pytest-embedded の制約に合わせ、各テストディレクトリでは親sketchをRX、`peer_tx/` をTXに固定します。peer名を `tx` に固定することで、ポート指定は既存の `TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3` を再利用できます。

## 構成

```text
compat_matrix/
  esp32irpk_self/       # RX: ESP32IRPulseKit, TX: ESP32IRPulseKit
    esp32irpk_self.ino
    peer_tx/
      peer_tx.ino
```

今後、外部ライブラリを追加する場合も親はRX、`peer_tx/` はTXのままにします。

```text
irremoteesp8266_tx/     # RX: ESP32IRPulseKit, TX: IRremoteESP8266
irremoteesp8266_rx/     # RX: IRremoteESP8266, TX: ESP32IRPulseKit
```

## 実行

```sh
cd tests
uv run --env-file .env pytest hardware/compat_matrix/esp32irpk_self/
```

