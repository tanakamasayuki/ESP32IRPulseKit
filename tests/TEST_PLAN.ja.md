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
| ローカル開発 | host、hardware/link_smoke、hardware/protocol_matrix |
| GitHub Actions | host、build、fixtures |
| 必要時 | hardware/compat_matrix、manual |

無指定の `pytest` は使わず、必ず `host`、`build`、`fixtures`、`hardware/link_smoke` のように対象の親ディレクトリを指定します。`hardware/` は実機とローカルSerialポートに依存するため、CI対象にはしません。

## 初期カバレッジ

| 機能 | host | build | hardware | manual | 状態 |
| --- | --- | --- | --- | --- | --- |
| NEC encode/decode roundtrip | ✅ | ✅ | ✅ NEC smoke | | host/build/2台smoke追加済み |
| NEC repeat encode/decode | ✅ | | ⬜ | | host smoke追加済み |
| SONY decode | ✅ | | ⬜ | | Sony12 fixture host test。SONY15/20はgenerated roundtrip + 生成式確認済み |
| Samsung decode | ✅ | | ⬜ | | Samsung32 fixture host test。SAMSUNG36はgenerated roundtrip + 生成式確認済み |
| JVC decode | ✅ | | ⬜ | | JVC24 fixture host test。JVC32はgenerated roundtrip + 生成式確認済み |
| Panasonic decode | ✅ | | ⬜ | | Panasonic48 fixture host test。PANASONIC40はgenerated roundtrip + 生成式確認済み |
| AEHA可変長encode/decode | ✅ | | ⬜ | | host smoke + MSB-first可変長test |
| RC5/RC6 decode | ✅ | | ⬜ | | RC5・RC6_M0・RC6_M6 fixture host test |
| protocol carrier推奨値 | ✅ | ✅ | ✅ NEC smoke | | 標準protocol値とsender override範囲をhostで確認 |
| 候補順位・score threshold | ✅ | | | | host smoke追加済み |
| encode拒否・不正入力 | ✅ | | | | バッファ不足・未知ID・bit長不一致 |
| RAWのみモード(候補0) | ✅ | | | | host smoke追加済み |
| tolerance境界 | ✅ | | | | SPACE_ENCの±25%境界をhost smokeで確認 |
| verified/generated fixture schema | ✅ | | | | YAML検査とgenerated候補の生成式確認を追加済み |
| examples build | | ✅ | | | buildテスト追加済み |
| RMT TX RAW送信 | | ✅ sketch build | ✅ NEC smoke | | TX peerスケッチ + 2台smoke |
| RMT RX RAW受信 | | ✅ sketch build | ✅ NEC smoke | | RX dutスケッチ + 2台smoke |
| TX->RX loop | | | ✅ NEC smoke | | TX/RX 2台構成のpytest追加済み |
| protocol matrix | | ✅ sketch build | ✅ | | ESP32IRPulseKit TX -> ESP32IRPulseKit RXでNEC/SONY12/SAMSUNG32/JVC24を確認 |
| 市販リモコン受信 | | | | ⬜ | 手動fixture化候補 |

## hardware構成

標準のhardwareテストは2台構成にします。

- TXボード: 既知の `protocol + bits` または `raw_ticks` を送信する
- RXボード: `IRReceiver` で受信し、Serialへdecode結果を出力する
- pytest: TX/RX両方のSerialを制御し、期待protocol/bits/scoreをassertする
- GPIO/反転設定: `.env` の `TEST_IR_TX_GPIO`、`TEST_IR_RX_GPIO`、`TEST_IR_TX_INVERTED`、`TEST_IR_RX_INVERTED` から `build_config.toml` 経由で注入する

`hardware/link_smoke/` はリリース判定用の安定smokeです。短時間で代表経路を確認し、通常のリリース前確認で実行します。

`hardware/protocol_matrix/` は自前TX -> 自前RXの複数protocol実機確認です。`link_smoke` より広くprotocol差分を見ます。通常のリリース前確認で実行します。

`hardware/compat_matrix/` は任意の互換性・差分調査用です。親sketchをRX、`peer_tx/` をTXに固定します。peer名を `tx` に固定することで、外部ライブラリ比較を増やしても `TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3` を使い回します。`compat_matrix` ではscore、raw_len、decode結果を観測ログとして残し、物理条件や外部ライブラリのtimerばらつきを評価します。

標準の自動hardware対象は当面 **ESP32-S3 2台構成** とします。ESP32 classic、ESP32-C3/C6など他SoCは、まず `examples/` とmanual確認で動作を見ます。特定SoCで差分や不具合が見つかった場合に、optional profileまたはmanual testとして昇格します。

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

- `SEND protocol bits`: `IRSender` と `IRReceiver` の統合経路を見る。初期実装は `SEND NEC <address_hex> <command_hex>`
- `SEND_RAW raw_ticks`: 既知波形に対するdecodeを見る

## 優先順

1. Arduino host基盤を選び、host実行テストを追加する
2. examplesと最小sketchのbuildテストを追加する
3. 2台構成のTX/RX実機テストを追加する
4. 市販リモコンで収集したRAW fixtureをhost/hardwareテストへ取り込む
