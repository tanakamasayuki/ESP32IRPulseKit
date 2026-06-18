# compat matrix: Arduino-IRremote RX

> English: [README.md](README.md)

RX: [Arduino-IRremote](https://github.com/Arduino-IRremote/Arduino-IRremote)、TX: ESP32IRPulseKit。

このバリアントは送信を自前の ESP32IRPulseKit、受信を Arduino-IRremote
`IrReceiver` で行います。peer TX (`peer_tx/`) は `hardware/protocol_matrix/` と
同じ ESP32IRPulseKit sketch で、差し替えるのは親RX sketchのみです。

- 親sketch (`arduino_irremote_rx.ino`): RX。`RX_READY impl=Arduino-IRremote ...` を出力
- `peer_tx/peer_tx.ino`: TX。`TX_READY impl=ESP32IRPulseKit ...` を出力し、pytest上では `peers["tx"]`

`DECODE_NEC` / `DECODE_SONY` / `DECODE_SAMSUNG` / `DECODE_JVC` のみ有効化しています。
Arduino-IRremote には score 指標が無いため `score=0` を出力します。protocol名は
自前の大文字表記（NEC / SONY<bits> / SAMSUNG<bits> / JVC<bits>）にマップしています。
Arduino-IRremote は標準的なactive-lowのIR受信モジュール前提で反転入力オプションが
無いため、`IR_RX_INVERTED` は表示のみで適用しません。

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
uv run --env-file .env pytest hardware/compat_matrix/arduino_irremote_rx/
```

pytestは送信protocol/bitsと、RXが観測したprotocol・bits・raw_len・`bit_order`
（same / reversed / other）を `COMPAT_MATRIX_OBSERVED` として出力します。
実装間でbit orderやfield解釈が異なる場合があるため、bitsの完全一致は**assertしません**。
フレームを認識できたこと (`raw_len > 0`) のみを必須とし、差分は観測ログとして記録します。

## 既知の失敗

2026-06-18 の実機ログ
`/tmp/pytest-embedded/2026-06-18_07-31-59-959191` では、以下が失敗しました。
失敗は通常のpytest失敗として残し、外部RXの対応範囲外なのか、PulseKit側の仕様差なのかを
このREADMEで追跡します。

| ケース | 観測 | 判定 |
|---|---|---|
| SAMSUNG36 | `RX_RAW len=76`。フレームは受信しているがArduino-IRremoteがdecodeしない | Arduino-IRremote 4.7.1 はSamsung32/Samsung48を扱うが、Samsung36専用decodeは見当たらない。外部RXの対応範囲外として扱う |
| JVC24 | `RX_RAW len=52`。フレームは受信しているがdecodeしない | Arduino-IRremoteの標準JVCは `JVC_BITS = 16`。PulseKitのJVC24は標準JVC RXの範囲外 |
| JVC32 | `RX_DECODE protocol=OTHER_9 len=32 bits=0x01234ABCD` | 32bit長の波形としては拾うがJVCとして分類しない。Arduino-IRremoteの標準JVCは16bitなので範囲外 |
