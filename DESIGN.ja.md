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

1. ヘッダー、bit長、mark/space列などで明らかな不一致を早期除外する
2. protocolの符号化規則でbit値を分類できる波形を候補として残す
3. nominal timingからの誤差を累積してscoreを付ける
4. score降順、同scoreなら登録順で候補を並べる
5. `setScoreThreshold()` 未満の候補は捨てる

`IRProtocolSpec::bit_tol_pct` は「良好一致」の基準であり、候補化の絶対上限ではありません。実IR受信ではTSOPなどの復調器でmark/spaceが系統的にずれるため、少し外れた信号は候補として残し、悪さをscoreへ載せます。

候補化の原則:

- フレームとして明らかに成立しないものは早期除外する
- header/repeat/header-like構造はprotocol識別に強く効くため、bit本体より保守的に扱う
- SPACE_ENCは、0/1 spaceが十分に離れていればnearest expected spaceでbit分類できる
- BIPHASEは、half-bit/grid構造が成立する範囲で候補化する
- bit分類が曖昧な領域、mark/space順序の破綻、bit数範囲外は候補から落とす

似たprotocolが同じRAWから複数候補に残ることは想定内です。最終的にはscore差、protocol固有の追加評価、登録順で順位を決めます。scoreの内訳は公開APIにしませんが、実装上は「strict windowを超えたら即棄却」ではなく「分類できる限り候補化し、誤差を累積する」方針を維持します。

scoreは公開API上の相対評価値です。絶対値の厳密な意味は固定しません。

scoreの内訳は公開APIにしません。通常利用では `score` と `decoded` を見ます。詳細診断が必要な場合は、将来 `ESP_LOGD` / `ESP_LOGV` のdecode traceとして出力します。

## 6. Protocol IDの考え方

`IRProtocolID` は波形timingだけでなく、`IRDecodedBits.bits` の論理解釈が互換かどうかで分けます。

例:

- Samsung 32bitと36bitは別ID
- AEHA は家製協ファミリ（可変長）を1つのIDにまとめる（Kaseikyo/Panasonic フレームも含む）

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
- 標準protocolでも推奨carrierを確認していないものは `0` のままにし、38kHzを一律には設定しない
- 現在の標準protocol推奨値は NEC/AEHA/Samsung=38kHz、JVC=37.9kHz、Sony=40kHz、RC5/RC6=36kHz
- `IRSender::setCarrierHz()` はsender単位の明示固定で、protocol推奨値より優先する
- `clearCarrierHz()` は明示固定を解除する
- duty比は公開APIにせず、内部固定の約1/3を使う
- begin後のcarrier変更は次回送信から反映する。送信中変更は拒否する
- 送受信hardware smokeはcarrier設定漏れを検出できる必要がある
- carrierは既定で位相整合・シンボル符号化。`setPhaseAlignedCarrier(false)` でフリーランニングのハードウェアキャリアに切替 — 第12節参照

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

- pc: 実機なしでPC上で動く自動テスト — `fixtures`（信号データ+検査）、`codec_smoke`（Arduino host実行）、`compile`（ESP32コンパイルのみ）
- hardware: ESP32実機2台でRMT TX/RXの合否回帰
- studies: 観測ログを取るオンデマンドの実機調査（自動収集されない）

## 12. キャリア生成とタイミングモデル

送受信とも ESP32 の **RMT** ペリフェラルで駆動します：ハードウェア決定的な包絡線
（mark/space）タイミング、非ブロッキング送信、捕捉エッジに割り込み遅延ジッターの
乗らないハード時刻印受信。RAWティックは10µsです。

### 位相整合キャリア（既定）

送信キャリアは **位相整合・シンボル符号化**です。送信チャネルを1µs解像度で動かし、
各マークを位相0始まりの整数個の完全キャリアサイクルとして送出します。よって各マークは
決定的なサイクル数を持ち、復調マーク（および直後のスペース）がフレーム間で安定します。
全プロトコルが仕様標準タイミングで送信されます（例：JVCゼロ空白525µs）。

トレードオフ:

- マーク幅はキャリア周期（38kHzで約26µs）に量子化される。標準IR許容（±25〜30%）の
  十分内側。
- 1フレームはキャリア1サイクル≒1シンボルに展開される（フレームあたり数百〜1000+）。
  RMTドライバがチャネルメモリからストリーム供給し、継ぎ足し余裕は
  `IRSender::setTxMemBlocks(n)`（1ブロック=`SOC_RMT_MEM_WORDS_PER_CHANNEL`シンボル）で
  決める。既定は1ブロックで通常の割り込み負荷ではクリーンに送れる。フラッシュ書込など
  ISR競合が重いアプリでは増やす。長尺フレームは一時バッファが大きくなる。

### ハードウェアキャリアへのフォールバック

`IRSender::setPhaseAlignedCarrier(false)` はフリーランニングのハードウェアキャリア
（`rmt_apply_carrier`）に切り替えます：シンボル数は大幅に少ないが、マーク毎に位相が
リセットされないため復調マークが±1キャリア周期（約26µs）ぶれる。この揺れは短マーク
プロトコル（JVC・AEHA等）を最狭の外部デコーダ窓（IRremoteESP8266）から外し得るため、
ライブラリ間相互運用には位相整合の既定が望ましい。根拠：
`tests/studies/{phase_aligned_carrier,carrier_loopback,jvc_timing_sweep}`。
