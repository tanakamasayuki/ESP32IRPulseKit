# Arduino host 実行テスト

> English: [README.md](README.md)

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

hostプロファイルは `lang-ship:host` を使います。各テストは `sketch.yaml` に `host` と必要に応じて `esp32` プロファイルを持ち、pytest-embedded + Arduino CLIで実行します。

```sh
uv run --env-file .env pytest host
uv run --env-file .env pytest host --profile=esp32
```
