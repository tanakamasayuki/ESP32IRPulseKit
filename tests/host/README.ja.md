# Arduino host 実行テスト

Arduino host 環境で実行するロジックテストをここに置きます。

目的は、Arduino API前提の環境に寄せたまま、RMT実機に依存しない処理をassertすることです。

対象:

- `Frame::toBits()` / `fromBits()`
- `encodeBitsToRaw()` / `decodeRawToBits()`
- protocol specの値
- tolerance境界
- score / candidate順位
- repeat decode
- fixture RAWから期待BITSへのdecode

RMT TX/RX、GPIO反転、idle threshold、実IR送受信は `tests/hardware/` で検証します。

Arduino host基盤は未確定です。選定後、このディレクトリに実行テストを追加します。
