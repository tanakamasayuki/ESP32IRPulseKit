# TX/RX 2台構成テスト

準備中です。

標準対象はESP32 classicです。ESP32-S3など他SoCの確認は、まず `examples/` とmanual確認で行います。

想定する接続:

- TX GPIO -> IR LEDドライバ
- RX GPIO <- IR受信モジュール
- TX/RXボードはUSBでPCへ接続

`.env` では次のポートを使う予定です。

```sh
TEST_SERIAL_PORT_TX_ESP32=/dev/ttyUSB0
TEST_SERIAL_PORT_RX_ESP32=/dev/ttyUSB1
TEST_IR_TX_GPIO=4
TEST_IR_TX_INVERTED=0
TEST_IR_RX_GPIO=32
TEST_IR_RX_INVERTED=1
```

GPIOは環境依存なので、スケッチに固定しません。`build_config.toml` で `.env` の `TEST_IR_*` をコンパイル時defineへ渡します。

| `.env` | sketch define | 意味 |
| --- | --- | --- |
| `TEST_IR_TX_GPIO` | `IR_TX_GPIO` | TXボードのIR LED出力GPIO |
| `TEST_IR_TX_INVERTED` | `IR_TX_INVERTED` | TX出力反転。通常は `0` |
| `TEST_IR_RX_GPIO` | `IR_RX_GPIO` | RXボードのIR受信モジュール入力GPIO |
| `TEST_IR_RX_INVERTED` | `IR_RX_INVERTED` | RX入力反転。一般的な受信モジュールは `1` |

pytestの想定フロー:

1. RXボードを待受状態にする
2. TXボードへ `SEND NEC 00ff 34` のようなコマンドを送る
3. RXボードのSerialからdecode結果を読む
4. protocol、bits、scoreをassertする

固定RAWを送る場合は `SEND_RAW` を使う想定です。`SEND protocol bits` は送受信統合経路、`SEND_RAW raw_ticks` は既知波形に対するdecode寄りの検証として分けます。
