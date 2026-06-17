# hardware compat matrix

> English: [README.md](README.md)

`compat_matrix/` は外部ライブラリや別実装との互換性・差分調査用です。自前TX -> 自前RXの複数protocol確認は `hardware/protocol_matrix/` で扱います。

pytest-embedded のpeer構成に合わせ、各テストディレクトリでは親sketchをRX、`peer_tx/` をTXに固定します。peer名を `tx` に固定することで、ポート指定は `TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3` を共通利用できます。

バリアント:

```text
irremoteesp8266_tx/     # RX: ESP32IRPulseKit, TX: IRremoteESP8266  (雛形)
irremoteesp8266_rx/     # RX: IRremoteESP8266, TX: ESP32IRPulseKit  (予定)
arduino_irremote_tx/    # RX: ESP32IRPulseKit, TX: Arduino-IRremote (予定)
arduino_irremote_rx/    # RX: Arduino-IRremote, TX: ESP32IRPulseKit (予定)
```

[`irremoteesp8266_tx/`](irremoteesp8266_tx/) が最初の雛形です。他のバリアントも
同じ構成に従います。

`compat_matrix` は任意実行です。score、raw_len、decode結果、raw timingのばらつきを観測し、bit orderやfield解釈の差分を記録する目的で使います。

