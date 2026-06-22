# Codec smoke（Arduino host）

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

hostプロファイルは `lang-ship:host` を使います。`sketch.yaml` に `host` と必要に応じて `esp32` プロファイルを持ち、pytest-embedded + Arduino CLIで実行します。

```sh
uv run --env-file .env pytest pc/codec_smoke
uv run --env-file .env pytest pc/codec_smoke --profile=esp32
```

`verified_fixtures.h` は `../fixtures/` から `../fixtures/export_cpp_fixtures.py` で生成します。手で編集しないでください。
