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

## リリース前チェック

リリース（**Release** GitHub Action の実行）前に、`.env` を設定した2ボードのリグで一式を回します。

```sh
uv run --env-file .env pytest -v                                                       # PCテスト＋例のコンパイル＋実機2台テスト
uv run --env-file .env pytest studies/compat_matrix*/ -o python_files="study_*.py" -v  # 実装間の互換（汎用＋エアコン）
```

- 1つ目は `pc/`（host のコーデック/フレームロジック・例＋スケッチのコンパイル・fixtures）**と** `hardware/`（`link_smoke`・`protocol_matrix`）を収集するため、**2ボードのリグが必要**です。`studies/` は収集しません（`test_*.py` ではなく `study_*.py` のため）。
- 2つ目は compat-matrix studies を外部ライブラリ（IRremoteESP8266・HeatpumpIR・Arduino-IRremote）に対して実行します — 各エアコンベンダの「対応」を裏付ける相互運用テストです。`compat_matrix*` は `compat_matrix/`（汎用プロトコル）と `compat_matrix_ac/`（エアコン）の両方に一致します。

この2回でリリースのゲート対象を網羅します: host ロジック・全例/スケッチのコンパイル・汎用2ボードのプロトコル相互運用・実装間互換。補足:

- 一部の compat ケースは**赤のままで正当**な場合があります — 実装間の真の非互換（例: 現行フレームを実装していない HeatpumpIR の変種）であり、回帰ではありません。self テスト（`*_self`）はデコード成否で**ゲートせず**、ボードが起動して送受信ループが回れば PASS します。
- リグが無い場合は host のみのサブセット — `uv run --env-file .env pytest pc -v`（codec_smoke＋例のコンパイル＋fixtures）。パス無しの `pytest -v` と studies コマンドはどちらもボードが必要です。

## ディレクトリ

- `pc/`: 実機なしでPC上で動く自動テスト（CIはこのフォルダ全体を実行）。
  - `pc/fixtures/`: 共有IR信号データとそのデータの検査。
  - `pc/codec_smoke/`: codec / protocol / frame のロジックをArduino hostビルドで実行。
  - `pc/compile/`: examplesと最小sketchのESP32向けコンパイル確認（ビルドのみ）。
- `hardware/`: ESP32実機2台で合否が出る自動テスト。`hardware/link_smoke` はリリース判定用の安定smoke。
- `studies/`: 実機で観測ログを取るオンデマンド調査（jitter、timing sweep、外部ライブラリ互換）。自動収集されず、判断には人手が要る。

## 環境設定

`.env.example` はテンプレートです。直接 `--env-file` に指定せず、`.env` へコピーしてローカル環境に合わせます。

```sh
cp .env.example .env
```
