# compat matrix: IRremoteESP8266 RX

> English: [README.md](README.md)

RX: [IRremoteESP8266](https://github.com/crankyoldgit/IRremoteESP8266)、TX: ESP32IRPulseKit。

このバリアントは送信を自前の ESP32IRPulseKit、受信を IRremoteESP8266 `IRrecv`
で行います。peer TX (`peer_tx/`) は `hardware/protocol_matrix/` と同じ
ESP32IRPulseKit sketch で、差し替えるのは親RX sketchのみです。

- 親sketch (`irremoteesp8266_rx.ino`): RX。`RX_READY impl=IRremoteESP8266 ...` を出力
- `peer_tx/peer_tx.ino`: TX。`TX_READY impl=ESP32IRPulseKit ...` を出力し、pytest上では `peers["tx"]`

IRremoteESP8266 には score 指標が無いため `score=0` を出力します。protocol名は
自前の大文字表記（NEC / SONY<bits> / SAMSUNG<bits> / JVC<bits>）にマップしています。
`IRrecv` は標準的なactive-lowのIR受信モジュール前提で反転入力オプションが無いため、
`IR_RX_INVERTED` は表示のみで適用しません。

## ケース

- NEC
- SONY12
- SONY15
- SONY20
- SAMSUNG32
- SAMSUNG36
- JVC
- RC5
- RC6_M0_16

## 実行

```sh
cd tests
uv run --env-file .env pytest hardware/compat_matrix/irremoteesp8266_rx/
```

pytestは送信protocol/bitsと、RXが観測したprotocol・bits・raw_len・`bit_order`
（same / reversed / other）を `COMPAT_MATRIX_OBSERVED` として出力します。
実装間でbit orderやfield解釈が異なる場合があるため、bitsの完全一致は**assertしません**。
フレームを認識できたこと (`raw_len > 0`) のみを必須とし、差分は観測ログとして記録します。
