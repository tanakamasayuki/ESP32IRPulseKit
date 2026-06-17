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
（`JITTER_GAP_MS`、既定5ms）を入れてシリアルを排出し `RX_JITTER` 行をクリーンに保ちます。
その後 `JITTER_LOOPBACK_OBSERVED` にエッジごとの
`mean_stdev_us` / `max_stdev_us` / `max_ptp_us` を出力します。1フレームの timing は
非常に再現性が高いので、回数は控えめで十分です。IRリンクとキャリアを除いているため、
ここでの差は送信側の生のタイミング精度（RMTハードDMA vs ソフトビットバンギング）を反映します。

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
test_arduino_irremote_loopback_jitter    500       0   500    67     0.xx    2.xx        x
test_irremoteesp8266_loopback_jitter     500       0   500    67     0.xx    2.xx        x
test_pulsekit_loopback_jitter            500       0   500    67     0.00    0.00        0
```

数値の読み方:

- `mean_sd` — エッジごと標準偏差の平均（典型的なジッター）。
- `max_sd` / `max_ptp` — 最悪の単一エッジ。大きい値は割り込み preempt スパイク
  （worst エッジの index/mean でどのパルスかを確認）。
- すべて `0.00` — 完全に決定論的なタイミング（RMTハードDMA）。
- `corrupt` — 宣言 `len=` と実際の `us=` 個数が不一致の行＝長いシリアル行が途中で
  バイト落ちしたもの。送信ジッターではなく**転送上のアーティファクト**で、破棄されます。
  スケッチは各行を1回の `write()` + `flush()` で出力し、これをほぼ0に抑えます。
- `used` — エッジ数が最頻長と一致したクリーンなフレーム（エッジ統計に使用）。

同じスクリプトは `hardware/tx_jitter/`（同じ `RX_JITTER` 形式）でも動くので、
有線ループバック vs 無線の比較にも使えます。
