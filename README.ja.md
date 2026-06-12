# ESP32IRPulseKit

ESP32 Arduino Core 3.x / ESP-IDF 5.x の新RMTドライバを使うIRリモコン送受信ライブラリです。

現在はリリース前の開発版です。外部仕様は [SPEC.ja.md](SPEC.ja.md)、テスト方針は [tests/TEST_PLAN.ja.md](tests/TEST_PLAN.ja.md) を参照してください。

## 現在の開発方針

- RAW tick capture、decode候補、protocol spec、frame変換を分離して整理する
- codec経路はArduino host実行テストで先に固める
- examplesや最小sketchはArduino CLIビルドテストで確認する
- RMT/HALはESP32実機テストで検証する
- 市販リモコンや外乱光など、ソフトウェアで完全制御できないものだけ手動テストに残す

## テスト

テストは `tests/` に集約します。

```sh
cd tests
uv run pytest host
```

`host/` はArduino host実行テスト、`build/` はArduino CLIビルドテスト、`hardware/` はESP32実機テストです。詳細は [tests/README.ja.md](tests/README.ja.md) を参照してください。

## 開発メモ

リリース前のため、APIや内部構造は破壊的変更を許容します。特に次の点は優先的に整理します。

- Arduino/RMT非依存のcodec層とESP32 HAL層の境界
- 可変長protocolのencode/decode対称性
- 対応済みprotocolと定義のみのprotocolの明示
- 自動テスト可能な実機TX/RX loopの整備
