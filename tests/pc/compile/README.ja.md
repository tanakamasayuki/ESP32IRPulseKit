# コンパイルテスト

> English: [README.md](README.md)

Arduino CLIでESP32向けにコンパイルできることだけを確認するテストをここに置きます。

対象:

- `examples/` のスケッチ
- 公開ヘッダをincludeする最小スケッチ
- codec / protocol / frame APIを使うcompile smokeスケッチ

このテストはビルド確認のみです。実行結果のassertは `tests/pc/codec_smoke/` または `tests/hardware/` で行います。

```sh
uv run --env-file .env pytest pc/compile
```
