# テスト計画

> English: [TEST_PLAN.md](TEST_PLAN.md)

## 方針

テストは4段階に分けます。

| 種別 | 目的 | 実行環境 |
| --- | --- | --- |
| host | codec、protocol spec、frame変換の実行assert | PC上のArduino host + pytest |
| build | examplesと最小sketchのESP32向けビルド確認 | PC上のpytest + Arduino CLI |
| hardware | RMT TX/RX、GPIO反転、idle threshold、queue/statの検証 | ESP32実機 + pytest-embedded |
| manual | 市販リモコン、距離/角度、外乱光などの確認 | 人が条件を確認 |

IRは物理環境の影響を受けやすいため、まずhostテストでArduino API前提のままRAW/BITS/Frameのロジックをassertします。buildテストではexamplesや公開ヘッダのESP32向けコンパイルを確認します。RMT依存部分はhardwareで検証します。

## 実行方針

| 環境 | 実行するテスト |
| --- | --- |
| ローカル開発 | host、hardware |
| GitHub Actions | host、build |
| 必要時 | manual |

## 初期カバレッジ

| 機能 | host | build | hardware | manual | 状態 |
| --- | --- | --- | --- | --- | --- |
| NEC encode/decode roundtrip | ✅ | ✅ | ⬜ | | host/build smoke追加済み |
| NEC repeat decode | ✅ | | ⬜ | | host smoke追加済み |
| SONY encode/decode roundtrip | ⬜ | | ⬜ | | 追加予定 |
| Samsung/JVC encode/decode roundtrip | ⬜ | | ⬜ | | 追加予定 |
| AEHA可変長decode | ⬜ | | ⬜ | | encode仕様整理後に追加 |
| 候補順位・score threshold | ✅ | | | | host smoke追加済み |
| tolerance境界 | ⬜ | | | | 追加予定 |
| verified fixture schema | ✅ | | | | YAML検査追加済み |
| examples build | | ⬜ | | | buildテストで追加 |
| RC5/RC6 decode | ⬜ | | ⬜ | | 対応範囲の再確認が必要 |
| RMT TX RAW送信 | | | ⬜ | | 2台構成で追加予定 |
| RMT RX RAW受信 | | | ⬜ | | 2台構成で追加予定 |
| TX->RX loop | | | ⬜ | | TX/RX 2台構成で追加予定 |
| 市販リモコン受信 | | | | ⬜ | 手動fixture化候補 |

## hardware構成

標準のhardwareテストは2台構成にします。

- TXボード: 既知の `protocol + bits` または `raw_ticks` を送信する
- RXボード: `IRReceiver` で受信し、Serialへdecode結果を出力する
- pytest: TX/RX両方のSerialを制御し、期待protocol/bits/scoreをassertする

1台loopbackは補助扱いにします。GPIO直結では実IR受信モジュールと反転条件が変わりやすく、標準の合否基準にはしません。

相手側に別の基準ライブラリは最初は使いません。基準は次の順で持ちます。

1. protocol specから生成した `protocol + bits`
2. 手書きまたはレビュー済みの固定 `raw_ticks`
3. 市販リモコンなどから採取し、レビュー済みに昇格した実測fixture
4. 外部ライブラリ比較は必要時の互換性テストとして扱う

## 信号データ

IR信号データは `tests/fixtures/` に置きます。

- `generated/`: protocol specとBITSから生成した理想波形
- `verified/`: 手書きまたはレビュー済みの固定RAW
- `captured/`: 市販リモコンや実機から採取した未整理RAW

hardwareテストでは2種類の送信を分けます。

- `SEND protocol bits`: `IRSender` と `IRReceiver` の統合経路を見る
- `SEND_RAW raw_ticks`: 既知波形に対するdecodeを見る

## 優先順

1. Arduino host基盤を選び、host実行テストを追加する
2. examplesと最小sketchのbuildテストを追加する
3. 2台構成のTX/RX実機テストを追加する
4. 市販リモコンで収集したRAW fixtureをhost/hardwareテストへ取り込む
