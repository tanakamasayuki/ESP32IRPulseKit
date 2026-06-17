# TX ジッター（有線ループバック・キャリアOFF）

> English: [README.md](README.md)

各IRライブラリの**純粋な送信タイミングジッター**を、IRリンクの影響を除いて測定します。
`hardware/tx_jitter/`（IR LED＋TSOP受信モジュール経由の無線。キャリア/復調歪みが乗る）
と違い、本リグは:

- **1台**で動作。送信と1µs RMT受信が同じクロックを共有。
- **キャリアOFF**（solid mark）。38kHz変調が線路に出ず、TSOP復調も介在しない。
- **直結配線**: ボード上でループバックTXピン→RXピンをジャンパ接続（同一ボードなので
  GND共通）。ピンは `.env` の `TEST_LOOPBACK_TX_GPIO` / `TEST_LOOPBACK_RX_GPIO`
  （既定 GPIO5 → GPIO6）から反映。他リグの `TEST_IR_*` とは別キーです。

各バリアントのスケッチが NEC送信とループバック信号の1µsキャプチャを兼ね、
`RX_JITTER seq=.. len=.. us=..` を出力します。

## バリアント

```text
pulsekit/          # TX: ESP32IRPulseKit (RMT)、disableCarrier() でキャリアOFF
irremoteesp8266/   # TX: IRremoteESP8266、use_modulation=false でキャリアOFF
arduino_irremote/  # TX: Arduino-IRremote、USE_NO_SEND_PWM でキャリアOFF
```

ESP32IRPulseKit には `IRSender::disableCarrier()` を追加してキャリアOFFに対応しました。

## 配線

1台でループバックTXピン→RXピンをジャンパ（既定 **GPIO5 → GPIO6**、
`.env` の `TEST_LOOPBACK_TX_GPIO` / `TEST_LOOPBACK_RX_GPIO` で指定）。
IR LED・受信モジュールは不要。

## 実行

```sh
cd tests
uv run --env-file .env pytest hardware/tx_jitter_loopback/
```

各バリアントが NEC を50回送信し（`JITTER_FRAMES` で上書き可）、フレーム間に小休止
（`JITTER_GAP_MS`、既定5ms）を入れます。その後 `JITTER_LOOPBACK_OBSERVED` にエッジごとの
`mean_stdev_us` / `max_stdev_us` / `max_ptp_us` を出力します。1フレームの timing は
非常に再現性が高いので、回数は控えめで十分です。IRリンクとキャリアを除いているため、
ここでの差は送信側の生のタイミング精度（RMTハードDMA vs ソフトビットバンギング）を反映します。

キャプチャをクリーンに保ち、バリアント間でフレーム数を揃えるための工夫が2点あります:

- **シリアル出力のペーシング**: `RX_JITTER` 1行は約400バイト。115200のUSB-UARTブリッジへ
  一気に流すと、ホスト/ブリッジの受信バッファが時々溢れて行の途中で数バイト落ちます。
  `dumpFrame()` は約16値ごとに `flush()`＋短い隙間を入れて分割送出し、ブリッジFIFOが
  詰まらないようにして corrupt 行をゼロにします。
- **初回キャプチャの priming**: 送信実装によっては arm 直後の**最初の1キャプチャを取り逃す**
  （ブロッキングなソフト送信が初回 arm とレースする）。各スケッチは `setup()` で1発だけ
  捨てフレームを送って行を出さずに drain するので、計測ループは安定状態から始まり、
  全バリアントがちょうど `JITTER_FRAMES` 個のフレームを記録します。

## 分析

`pytest` は variant ごとに `JITTER_LOOPBACK_OBSERVED` の1行サマリを出します（`-rA` /
`-s` 付き、または `record_property` 経由でJUnit/HTMLレポートに残る）。エッジ単位の
詳細は `analyze.py` を各 `dut.log` に対して実行します。生の `RX_JITTER` 行から統計を
再計算します:

```sh
# 最新の pytest-embedded 実行を自動検出し、*jitter* ログをすべて解析
uv run python hardware/tx_jitter_loopback/analyze.py

# 結果ディレクトリ / dut.log を明示し、worst エッジを増やす
uv run python hardware/tx_jitter_loopback/analyze.py --worst 10 \
    /tmp/pytest-embedded/<run>/test_arduino_irremote_loopback_jitter
```

出力は比較表＋送信側ごとの worst エッジ:

```text
capture                                clean corrupt  used edges  mean_sd  max_sd  max_ptp
test_arduino_irremote_loopback_jitter     50       0    50    67     0.77    2.65        9
test_irremoteesp8266_loopback_jitter      50       0    50    67     0.40    2.92        6
test_pulsekit_loopback_jitter             50       0    50    67     0.00    0.00        0
```

数値の読み方:

- `mean_sd` — エッジごと標準偏差の平均（典型的なジッター）。
- `max_sd` / `max_ptp` — 最悪の単一エッジ。大きい値は割り込み preempt スパイク
  （worst エッジの index/mean でどのパルスかを確認）。
- すべて `0.00` — 完全に決定論的なタイミング（RMTハードDMA）。
- `corrupt` — 宣言 `len=` と実際の `us=` 個数が不一致の行＝長いシリアル行が途中で
  バイト落ちしたもの。送信ジッターではなく**転送上のアーティファクト**で、破棄されます。
  分割ペーシング出力（「実行」参照）でこれを0に抑えています。
- `used` — エッジ数が最頻長と一致したクリーンなフレーム（エッジ統計に使用）。

同じスクリプトは `hardware/tx_jitter/`（同じ `RX_JITTER` 形式）でも動くので、
有線ループバック vs 無線の比較にも使えます。

## 観測結果

代表的なラン（NEC、単板キャリアOFFループバック、ESP32-S3 2台、無負荷）。絶対値は
構成依存で、見るべきは相対関係です。

| TX library | 生成方式 | mean_sd | max_sd | max_ptp |
| --- | --- | --: | --: | --: |
| ESP32IRPulseKit | RMT（ハードDMA） | **0.00 µs** | 0.00 | 0 |
| IRremoteESP8266 | ソフト `delayMicroseconds` | ~0.3-0.4 µs | ~1.5-2.9 | ~6-9 |
| Arduino-IRremote | ソフト `delayMicroseconds` | ~0.7-0.8 µs | ~2.6-3.0 | ~9-11 |

発見:

- **RMT（ESP32IRPulseKit）は完全に決定論的**。全フレームの全エッジが一致（0µs
  ジッター）。値はライブラリの10µs RMTティックに量子化されるが、完全に再現する。
- **ソフト/`delayMicroseconds` 送信は定常状態でサブµsのジッター**だが、割り込み
  preempt に弱い。初期の50フレームのランで Arduino-IRremote が1エッジだけ
  `max_ptp ≈ 800 µs`（`sd ≈ 114 µs`）に跳ねたのを観測（単発のISR preempt）。無負荷では
  稀だが、WiFi/BLE/他ISR下では頻度が上がる。RMTは負荷に関係なく0のまま。
- いずれもプロトコル許容（NEC ±25% / ~560µs）に対して極小で、クリーンなリンクなら
  3つとも問題なくデコードできる。RMTの利点は定常精度ではなく**負荷下での堅牢性**。

手法メモ: 無線リグ（`hardware/tx_jitter/`）では IR LED + TSOP のキャリア復調が支配的で、
ばらつきが大きく（数〜18µs）真のTX順位を覆い隠し・反転させていた。キャリアとTSOPを
除いた本リグで初めて送信側本来のタイミングが見える。
