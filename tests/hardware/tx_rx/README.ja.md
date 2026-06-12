# TX/RX 2台構成テスト

準備中です。

想定する接続:

- TX GPIO -> IR LEDドライバ
- RX GPIO <- IR受信モジュール
- TX/RXボードはUSBでPCへ接続

`.env` では次のポートを使う予定です。

```sh
TEST_SERIAL_PORT_TX_ESP32=/dev/ttyUSB0
TEST_SERIAL_PORT_RX_ESP32=/dev/ttyUSB1
```

pytestの想定フロー:

1. RXボードを待受状態にする
2. TXボードへ `SEND NEC 00ff 34` のようなコマンドを送る
3. RXボードのSerialからdecode結果を読む
4. protocol、bits、scoreをassertする

固定RAWを送る場合は `SEND_RAW` を使う想定です。`SEND protocol bits` は送受信統合経路、`SEND_RAW raw_ticks` は既知波形に対するdecode寄りの検証として分けます。
