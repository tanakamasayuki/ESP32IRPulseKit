# テスト

> English: [README.md](README.md)

ESP32IRPulseKit のテストは、ソフトウェアだけで完全に制御できるかどうかで分類します。全体方針とカバレッジ表は [TEST_PLAN.ja.md](TEST_PLAN.ja.md) を参照してください。

## 必要なもの

- `uv`
- hostテスト: Arduino host実行環境
- buildテスト: Arduino CLI、ESP32 Arduino Core
- hardwareテスト: Arduino CLI、ESP32 Arduino Core、対象ボード

## 実行

`tests/` ディレクトリから実行します。

```sh
# Arduino host実行テスト
uv run pytest host

# Arduino CLIビルドテスト
uv run pytest build

# 実機テストも含めて実行する場合
uv run --env-file .env pytest
```

## ディレクトリ

- `host/`: Arduino hostで実行するロジックテスト。codec、protocol spec、frame変換などをassertします。
- `build/`: Arduino CLIビルドテスト。examplesや最小sketchがESP32向けにコンパイルできることを検証します。
- `hardware/`: ESP32実機を使う自動テスト。TX/RX loopなど、RMTを含む経路を検証します。
- `manual/`: 市販リモコン、外乱光、距離/角度など、環境を完全制御しにくい検証を置きます。
- `fixtures/`: host/hardware/manualで共有するIR信号データを置きます。

## シリアルポート設定

実機テストを追加する段階で `.env.example` の値をローカル環境に合わせます。

```sh
cp .env.example .env
```
