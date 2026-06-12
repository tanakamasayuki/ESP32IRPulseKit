# ビルドテスト

> English: [README.md](README.md)

Arduino CLIでESP32向けにコンパイルできることを確認するテストをここに置きます。

対象:

- `examples/` のスケッチ
- 公開ヘッダをincludeする最小スケッチ
- codec / protocol / frame APIを使うcompile smokeスケッチ

このテストはビルド確認のみです。実行結果のassertは `tests/host/` または `tests/hardware/` で行います。

GitHub Actionsでは `host` と `build` を実行する想定です。
