# ESP32IRPulseKit 設計メモ

> English: [DESIGN.md](DESIGN.md)

この文書は実装者向けの設計メモです。外部API契約は [SPEC.ja.md](SPEC.ja.md) に置きます。

## 1. 設計方針

- 受信API: `esp32irpk::IRReceiver`
- 送信API: `esp32irpk::IRSender`
- RMT依存処理は `src/hal/` に閉じ込める
- RAW/BITS変換は `src/codec/` に置く
- protocol定義とFrame型は `src/protocols/` に置く
- Frame型は論理値と `IRDecodedBits` の変換だけを担当し、decode/encode本体は持たない

## 2. データフロー

受信:

```text
RMT RX -> RAW ticks -> decode -> IRDecodedBits -> Frame::fromBits()
```

送信:

```text
Frame::toBits() -> IRDecodedBits -> encode -> RAW ticks -> RMT TX
```

RAWは `1 tick = 10us` です。protocol specはマイクロ秒で持ちます。

## 3. idle threshold と gap threshold

`idle_threshold_us` はRMTが無信号として受信を切る閾値です。`gap_threshold_us` はcodecが複数フレーム連結RAWを分割する閾値です。

両者は独立です。

- RMT idle thresholdが長いと、複数フレームが1つのRAWに連結されることがある
- codecは各protocolの `gap_threshold_us` 以上のspaceをgapとして扱う
- gapは分割には使うが、score減点には使わない

## 4. 受信キューと consumed_len

HALはRMTで受信したシンボル列をtick配列に変換し、内部キューへ積みます。

`IRReceiver::read()` は最上位候補の `consumed_len` を使って、処理済みRAWの長さを進めます。1つのRMT受信に複数フレームが含まれている場合、次回 `read()` で残りを処理します。

キューが満杯の場合は古い要素をdropし、`queue_overflow_count` を加算します。

## 5. Decode候補とscore

decodeは登録済みprotocolすべてに対して行います。

大まかな流れ:

1. ヘッダー、bit長、mark/space列などで早期除外する
2. 成立した候補にscoreを付ける
3. score降順、同scoreなら登録順で候補を並べる
4. `setScoreThreshold()` 未満の候補は捨てる

scoreは公開API上の相対評価値です。絶対値の厳密な意味は固定しません。

scoreの内訳は公開APIにしません。通常利用では `score` と `decoded` を見ます。詳細診断が必要な場合は、将来 `ESP_LOGD` / `ESP_LOGV` のdecode traceとして出力します。

## 6. Protocol IDの考え方

`IRProtocolID` は波形timingだけでなく、`IRDecodedBits.bits` の論理解釈が互換かどうかで分けます。

例:

- Samsung 32bitと36bitは別ID
- Panasonic 40bitと48bitは別ID
- AEHAのように同一Frame解釈で可変長を扱えるものは1つのIDにまとめる

## 7. repeat_count

`repeat_count` は追加送信回数です。

- `0`: 1回だけ送信
- `2`: 初回 + 追加2回 = 合計3回送信
- `-1`: `IRProtocolSpec::default_repeat_count` を使う

SONY系は合計3フレームが必要な機器があるため、`default_repeat_count = 2` とします。

## 8. 送信carrier

RMT TXは通常のIR受信モジュールで受信できるよう、carrier変調を有効にします。

- RAW/BITSのtick列はmark/spaceの包絡線を表す
- HALはmark区間にcarrierを重畳してGPIOへ出力する
- ライブラリ既定値は38kHz
- `IRProtocolSpec::carrier_hz` はprotocol推奨値を表す。`0` は既定値を使う
- 標準protocolでも推奨carrierを確認していないものは `0` のままにし、38kHzを一律には明示しない
- `IRSender::setCarrierHz()` はsender単位の明示固定で、protocol推奨値より優先する
- `clearCarrierHz()` は明示固定を解除する
- duty比は公開APIにせず、内部固定値を使う
- begin後のcarrier変更は次回送信から反映する。送信中変更は拒否する
- 送受信hardware smokeはcarrier設定漏れを検出できる必要がある

## 9. ログ方針

ESP32実機ではESP-IDFの `ESP_LOGx` 系を使います。

- `E`: begin失敗、RMTチャネル確保失敗など処理継続できない問題
- `W`: truncate、overflow、容量不足など処理は継続できる問題
- `I`: begin/end、protocol自動登録、idle threshold決定など主要状態
- `D`: decode成功/失敗理由、score、送信要求などの診断情報
- `V`: RAW tick列やRMTシンボル列など大量データ

通常運用では `D` までを実用ログ、`V` は問題解析時のみ使います。

## 10. コメント規約

- ライブラリ実装内のコメントは英語
- examplesやテストREADMEは必要に応じて日本語/英語を分ける
- サンプルコードでは `using namespace` を避け、`esp32irpk::` を明示する

## 11. テスト方針

テスト全体の方針は [tests/TEST_PLAN.ja.md](tests/TEST_PLAN.ja.md) に置きます。

- host: Arduino hostでcodec/Frame/fixtureを実行assert
- build: examplesと最小sketchをESP32向けにcompile
- hardware: ESP32実機2台でRMT TX/RX経路を検証
- manual: 市販リモコン、距離、外乱光など人手が必要な確認
