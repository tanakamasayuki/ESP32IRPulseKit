# compat matrix: IRremoteESP8266 TX

> English: [README.md](README.md)

RX: ESP32IRPulseKit、TX: [IRremoteESP8266](https://github.com/crankyoldgit/IRremoteESP8266)。

このバリアントはIR LEDの送信をIRremoteESP8266で行い、受信を自前の
ESP32IRPulseKit `IRReceiver` でデコードします。シリアルプロトコルは
`hardware/protocol_matrix/` と同じで、自前TXからの差分は `peer_tx/` の
sketchのみです。

- 親sketch (`irremoteesp8266_tx.ino`): RX。`RX_READY impl=ESP32IRPulseKit ...` を出力
- `peer_tx/peer_tx.ino`: TX。`TX_READY impl=IRremoteESP8266 ...` を出力し、pytest上では `peers["tx"]`

## ケース

- NEC
- SONY12
- SONY15
- SONY20
- SAMSUNG32
- SAMSUNG36
- JVC

## 実行

```sh
cd tests
uv run --env-file .env pytest hardware/compat_matrix/irremoteesp8266_tx/
```

pytestは送信したprotocol/bitsと、RXが観測したprotocol・bits・score・raw_lenを
`COMPAT_MATRIX_OBSERVED` として出力します。実装間でbit orderやfield解釈が
異なる場合があるため、bitsの完全一致は**assertしません**。フレームを認識できたこと
(`raw_len > 0`) のみを必須とし、protocol/bitsの差分は観測ログ（`bits_match`）
として記録します。
