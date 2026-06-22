# PC テスト

> English: [README.md](README.md)

ESP32実機なしでPC上で実行する自動テストです。このグループはCIで実行します。

| サブディレクトリ | 目的 |
| --- | --- |
| `fixtures/` | 共有IR信号データ（`verified/*.yaml`）とそのデータの検査 |
| `codec_smoke/` | codec / protocol / Frame のロジックをArduino hostビルドで実行しassert |
| `compile/` | examplesと最小sketchをESP32向けにコンパイル（ビルドのみ・実行しない） |

グループ全体、または個別サブディレクトリで実行します。

```sh
uv run --env-file .env pytest pc
uv run pytest pc/fixtures
uv run --env-file .env pytest pc/codec_smoke
uv run --env-file .env pytest pc/compile
```

`fixtures/export_cpp_fixtures.py` が `codec_smoke/verified_fixtures.h` を生成し、codec smoke sketchがそれを読むので両者は同期します。

実RMT TX/RX とIR送受信は `tests/hardware/` で検証します。
