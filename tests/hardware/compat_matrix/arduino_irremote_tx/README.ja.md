# compat matrix: Arduino-IRremote TX

> English: [README.md](README.md)

RX: ESP32IRPulseKit、TX: [Arduino-IRremote](https://github.com/Arduino-IRremote/Arduino-IRremote)。

このバリアントはIR LEDの送信をArduino-IRremoteで行い、受信を自前の
ESP32IRPulseKit `IRReceiver` でデコードします。シリアルプロトコルは
`hardware/protocol_matrix/` と同じで、自前TXからの差分は `peer_tx/` の
sketchのみです。

- 親sketch (`arduino_irremote_tx.ino`): RX。`RX_READY impl=ESP32IRPulseKit ...` を出力
- `peer_tx/peer_tx.ino`: TX。`TX_READY impl=Arduino-IRremote ...` を出力し、pytest上では `peers["tx"]`

Arduino-IRremote の新しい送信APIは address/command 指向のため、任意の生payloadを
送るには非推奨のMSB-first raw送信関数
(`sendNECMSB`/`sendSonyMSB`/`sendSamsungMSB`/`sendJVCMSB`) を使っています。
また反転出力オプションが無いので、`IR_TX_INVERTED` は表示のみで適用しません。

## ケース

- NEC
- SONY12
- SONY15
- SONY20
- SAMSUNG32
- SAMSUNG36
- JVC24
- JVC32

## 実行

```sh
cd tests
uv run --env-file .env pytest hardware/compat_matrix/arduino_irremote_tx/
```

pytestは送信protocol/bitsと、RXが観測したprotocol・bits・score・raw_len・
`bit_order`（same / reversed / other）を `COMPAT_MATRIX_OBSERVED` として出力します。
実装間でbit orderやfield解釈が異なる場合があるため、bitsの完全一致は**assertしません**。
フレームを認識できたこと (`raw_len > 0`) のみを必須とし、差分は観測ログとして記録します。
