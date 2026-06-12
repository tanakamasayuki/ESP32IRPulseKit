# 実機自動テスト

> English: [README.md](README.md)

ESP32実機を使う自動テストをここに追加します。

標準構成は2台構成です。

- TXボード: `IRSender` で既知のRAW/BITSを送信する
- RXボード: `IRReceiver` で受信し、decode結果をSerialへ出力する
- pytest: 両方のSerialを制御し、送信要求と受信結果を照合する

IR LEDと受信モジュールを近距離で向かい合わせ、外乱を減らした固定治具で実行する想定です。

標準の自動hardware対象は当面ESP32 classicのみです。他SoC（ESP32-S3、ESP32-C3/C6など）は `examples/` とmanual確認で扱い、必要になったらoptional profileへ昇格します。

1台loopbackは補助扱いです。GPIO直結では実IR受信モジュール経由と反転条件が変わりやすいため、標準の合否基準にはしません。

相手側に別の基準IRライブラリは最初は使いません。基準データは `tests/fixtures/` に置き、`protocol + bits` と `raw_ticks` を使い分けます。

GPIO番号や反転設定は環境依存のため `.env` で管理します。2台構成では `TEST_IR_TX_GPIO`、`TEST_IR_RX_GPIO`、`TEST_IR_TX_INVERTED`、`TEST_IR_RX_INVERTED` を使います。
