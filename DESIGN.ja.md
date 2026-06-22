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
- 標準protocolでも推奨carrierを確認していないものは `0` のままにし、38kHzを一律には設定しない
- 現在の標準protocol推奨値は NEC/AEHA/Panasonic/Samsung=38kHz、JVC=37.9kHz、Sony=40kHz、RC5/RC6=36kHz
- `IRSender::setCarrierHz()` はsender単位の明示固定で、protocol推奨値より優先する
- `clearCarrierHz()` は明示固定を解除する
- duty比は公開APIにせず、内部固定の約1/3を使う
- begin後のcarrier変更は次回送信から反映する。送信中変更は拒否する
- 送受信hardware smokeはcarrier設定漏れを検出できる必要がある
- carrierはRMTハードウェアが生成し、マーク毎の位相は制御できない — その帰結（と唯一のJVC例外）は第12節参照

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

## 12. タイミングモデル：RMT vs タイマー と キャリア位相の限界

本ライブラリは送受信とも ESP32 の **RMT** ペリフェラルで駆動します。この選択が
タイミング特性を決めています ── 包絡線タイミングとCPU負荷で強く、キャリア位相だけが
弱点。そのトレードオフと、それが強いる唯一のプロトコル例外を記録します。

### RMTで得られるもの

- **決定的な包絡線（mark/space）タイミング**。継続時間はソフトループではなくハード
  ウェア由来なので、フレーム間ジッターが小さく、送信中にCPUを占有しない。受信は
  ハードウェアで時刻印され、捕捉エッジに割り込み遅延ジッターが乗らない。
- **内部10µsティック**。ドライバは100kHz解像度で動くため、送出/復調の継続時間は
  10µs量子化される。標準IRの許容（±25〜30%＝数十〜数百µs）に対して十分内側なので
  デコードには無問題。ただし JVC の 525µs は 530µs（53tick）として送出される。

### キャリア位相の限界

38kHzキャリアはRMTハードウェア（`rmt_apply_carrier`）が **フリーランニングの発振を
マークにゲートして**生成する。**マーク境界でキャリア位相をリセットするAPIは無い**。
そのため、マーク内に収まる完全キャリアサイクル数がマーク毎に **±1（38kHzで約26µs）**
ぶれる（マーク端がランダムな位相で切れるため）。これは送信側の特性だが受信側で顕在化
する：復調器（TSOP）がサイクルを数えるので、復調マーク長 ── ひいては直後のスペース ──
が最大1キャリア周期ずれる。

- **大半のプロトコルでは無害**：受信窓に±26µsの揺れを吸収する余裕がある。
- **唯一の例外がJVC**。最も厳しい外部デコーダ（IRremoteESP8266: `(526−50)×1.25 ≈
  594µs`）のゼロ空白窓が全プロトコル中最狭で、かつ 525→530µs マークが半整数サイクル点
  （最悪の揺れ）に乗る。そこで PulseKit は意図的に **非標準の 480µs JVCゼロ空白**
  （仕様525µs）を送出して受信スペースを窓から離す。これは送信マージンの回避策で、
  `src/protocols/JVC.h` に明記。他プロトコルは仕様標準値を使う。本来の対策は ~1µs
  解像度の位相整合シンボルエンコードキャリアだが大改修のため先送り。根拠は
  `tests/hardware/{carrier_loopback,jvc_timing_sweep,jvc_verify_arduino}`。

### なぜタイマー系ライブラリはJVC調整が要らないか

Arduino-IRremote や IRremoteESP8266 はキャリアを **ソフトウェア**で生成する ── 各マーク
の先頭で（再）開始するビジーループ（またはPWM）。キャリアがマークと共に再開するため
位相が揃い、各マークが整数・決定的なサイクル数を持つ ── よって **キャリア位相ジッターが
無く、JVC調整も不要**。（当方のセルフテストでも確認：IRremoteESP8266→IRremoteESP8266 の
JVC は 5/5 でデコード。）代償は別の場所：ソフトキャリアは**フレーム全体でCPUをビジー
ループ占有**し、受信は**タイマー/エッジ割り込み**なので、捕捉した包絡線タイミングに
割り込み遅延とティック量子化が乗り、他割り込みで乱れうる。

### RMT vs タイマー ── 強みと弱み

| 観点 | RMT（本ライブラリ） | タイマー/ソフトキャリア（他） |
|---|---|---|
| 包絡線タイミング | ハード決定的・低ジッター | ISR遅延/ビジーループのスケジュールジッター |
| 送信中のCPU | 解放（ハード駆動・非ブロッキング） | フレーム全体をビジーループ占有 |
| 受信捕捉 | ハード時刻印・ISRジッター無 | タイマー/エッジISR → 遅延＋ティック量子化 |
| 時間解像度 | 細かいクロック・内部10µsティック | やや粗いがキャリアは位相整合 |
| キャリア位相 | フリーラン・リセット不可 → ±1周期ジッター | マーク毎に再開 → 位相整合・ジッター無 |
| JVC | 480µsゼロ空白の回避策が必要 | 仕様タイミングで動作・調整不要 |
| マルチチャネル | 独立RMTチャネル複数 | 限定的（タイマー/CPU依存） |

**結論**：RMTは現代のESP32ライブラリに適する ── 決定的・非ブロッキング・マルチ
チャネル・低ジッターな包絡線。唯一の実弱点はキャリア位相が制御不能な点で、それが
噛むのは厳密に1つのプロトコル/受信機の組み合わせ（最狭窓のJVC）だけ。1つの文書化された
タイミング調整で対処している。タイマー/ソフト系が勝るのはこのキャリア位相の点のみで、
CPU占有と包絡線タイミングの悪化を代償に払う。
