# テスト

> English: [README.md](README.md)

ESP32IRPulseKit のテストは「どう走らせるか（PCで自動 / 実機で合否 / 人が観測）」で分類します。全体方針とカバレッジ表は [TEST_PLAN.ja.md](TEST_PLAN.ja.md) を参照してください。

## 必要なもの

- `uv`
- `pc/codec_smoke`: Arduino CLI、`lang-ship:host`
- `pc/compile`: Arduino CLI、ESP32 Arduino Core
- `hardware/`: Arduino CLI、ESP32 Arduino Core、対象ボード

## 実行

`tests/` ディレクトリから実行します。トップレベルのフォルダ単位でまとめて走らせられます。
最初に `.env.example` を `.env` へコピーし、ローカル環境のSerialポートとGPIOに合わせて編集します。

```sh
cp .env.example .env

# PCテスト一式（CIはこれを実行）
uv run --env-file .env pytest pc

# 個別サブディレクトリでも可
uv run pytest pc/fixtures

# ESP32実機2台の合否回帰テスト
uv run --env-file .env pytest hardware/link_smoke
```

`pytest .` や `pytest hardware` は `studies/` 配下を**収集しません**。studies のファイルは `test_*.py` ではなく `study_*.py` だからです。必要時はパターンを明示して実行します。

```sh
uv run --env-file .env pytest -s -o python_files="study_*.py" studies/carrier_jitter/
```

## ディレクトリ

- `pc/`: 実機なしでPC上で動く自動テスト（CIはこのフォルダ全体を実行）。
  - `pc/fixtures/`: 共有IR信号データとそのデータの検査。
  - `pc/codec_smoke/`: codec / protocol / frame のロジックをArduino hostビルドで実行。
  - `pc/compile/`: examplesと最小sketchのESP32向けコンパイル確認（ビルドのみ）。
- `hardware/`: ESP32実機2台で合否が出る自動テスト。`hardware/link_smoke` はリリース判定用の安定smoke。
- `studies/`: 実機で観測ログを取るオンデマンド調査（jitter、timing sweep、外部ライブラリ互換）。自動収集されず、判断には人手が要る。
- `manual/`: 市販リモコン、距離/角度、外乱光など人手の確認を置きます。

## 環境設定

`.env.example` はテンプレートです。直接 `--env-file` に指定せず、`.env` へコピーしてローカル環境に合わせます。

```sh
cp .env.example .env
```
