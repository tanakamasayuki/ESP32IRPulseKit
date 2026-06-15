# TX/RX 2台構成テスト

> English: [README.md](README.md)

標準対象はESP32-S3 2台構成です。ESP32 classicなど他SoCの確認は、まず `examples/` とmanual確認で行います。

このディレクトリはpytest-embeddedのpeer構成です。

- `tx_rx.ino`: RXボード。pytest上では `dut`
- `peer_tx/peer_tx.ino`: TXボード。pytest上では `peers["tx"]`

想定する接続:

- TX GPIO -> IR LEDドライバ
- RX GPIO <- IR受信モジュール
- TX/RXボードはUSBでPCへ接続

`.env` では次のポートとGPIOを設定します。

```sh
TEST_SERIAL_PORT_TX_ESP32S3=/dev/ttyUSB0
TEST_SERIAL_PORT_RX_ESP32S3=/dev/ttyUSB1
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

## Serialプロトコル

TX/RXは起動後に次のready行を出します。

```text
TX_READY gpio=<gpio> inverted=<0|1>
RX_READY gpio=<gpio> inverted=<0|1>
```

共通コマンド:

| コマンド | 応答 |
| --- | --- |
| `PING` | `PONG` |

TXコマンド:

| コマンド | 成功応答 |
| --- | --- |
| `SEND NEC <address_hex> <command_hex>` | `TX_OK NEC <address_hex> <command_hex>` |

RX出力:

| 出力 | 意味 |
| --- | --- |
| `RX_RAW len=<n>` | decode候補なしでRAWのみ受信 |
| `RX_DECODE protocol=<name> score=<score> len=<bits> bits=0x<hex> type=<NORMAL|REPEAT>` | decode候補の先頭 |

## pytestの流れ

1. RXボードを待受状態にする
2. TXボードへ `SEND NEC 00ff 34` のようなコマンドを送る
3. RXボードのSerialからdecode結果を読む
4. protocol、bits、scoreをassertする

実行例:

```sh
cd tests
uv run --env-file .env pytest hardware/tx_rx
```

固定RAWを送る場合は `SEND_RAW` を使う想定です。`SEND protocol bits` は送受信統合経路、`SEND_RAW raw_ticks` は既知波形に対するdecode寄りの検証として分けます。
