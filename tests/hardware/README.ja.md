# 実機自動テスト

> English: [README.md](README.md)

ESP32実機を使う自動テストをここに追加します。

## ディレクトリ

| パス | 目的 | リリース判定 |
| --- | --- | --- |
| `link_smoke/` | 自前ライブラリの2台IRリンクが最低限動くことを確認する安定smoke | 必須 |
| `protocol_matrix/` | 自前TX -> 自前RXで複数protocolを実機確認するmatrix | 必須寄り |

これらは合否が明確で、`pytest hardware` で自動収集されます。観測ログを取るだけの実機調査（jitter、timing sweep、外部ライブラリ互換）は `tests/studies/` に置き、自動収集されません。

`protocol_matrix/` は親sketchをRX、`peer_tx/` をTXに固定します。peer名を `tx` に固定することで、ポート指定は `TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3` を共通利用できます。

標準構成は2台構成です。

- TXボード: `IRSender` で既知のRAW/BITSを送信する
- RXボード: `IRReceiver` で受信し、decode結果をSerialへ出力する
- pytest: 両方のSerialを制御し、送信要求と受信結果を照合する

IR LEDと受信モジュールを近距離で向かい合わせ、外乱を減らした固定治具で実行する想定です。

標準の自動hardware対象は当面ESP32-S3 2台構成です。他SoC（ESP32 classic、ESP32-C3/C6など）は `examples/` とmanual確認で扱い、必要になったらoptional profileへ昇格します。

1台loopbackは補助扱いです。GPIO直結では実IR受信モジュール経由と反転条件が変わりやすいため、標準の合否基準にはしません。

`link_smoke/` では相手側に別の基準IRライブラリを使いません。基準データは `tests/pc/fixtures/` に置き、`protocol + bits` と `raw_ticks` を使い分けます。

GPIO番号や反転設定は環境依存のため `.env` で管理します。2台構成では `TEST_IR_TX_GPIO`、`TEST_IR_RX_GPIO`、`TEST_IR_TX_INVERTED`、`TEST_IR_RX_INVERTED` を使います。
