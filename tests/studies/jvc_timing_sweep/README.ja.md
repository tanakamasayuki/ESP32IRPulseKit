# JVC タイミングスイープ（マーク × ゼロ空白）

> English: [README.md](README.md)

JVCの送出タイミングのうち、**IRremoteESP8266** のデコーダを一番安定して通す値を探す
リグです。**ビットマーク**と**ゼロ空白**を2Dグリッドで振り、各セルで受信ゼロ空白の
余裕とデコード成功率を測ります。これは [../carrier_loopback/](../carrier_loopback/)
の根本原因調査（フリーランキャリア位相）を補完する「安価なミティゲーション探索」です。

- **TX（peer_tx/）**：ESP32IRPulseKit。`JVCRAW <mark_us> <zero_space_us> <one_space_us>
  <hex_bits>` で、指定タイミングのJVC形フレーム（ヘッダ8400/4200固定、16bit LSB-first、
  トレーラマーク）をライブラリ既定キャリア（38kHz/0.33）で送信。
- **RX（jvc_timing_sweep.ino）**：IRremoteESP8266。**毎フレーム**生キャプチャ(µs)を
  ダンプするので、デコード成功時でも受信ゼロ空白が読める。

経路は IR LED → 空中 → TSOP（2台、`compat_matrix/` と同配線）。

## なぜ2D

受信ゼロ空白 ≈ `space_env + (mark_env − 復調マーク長)`。**ゼロ空白**は受信スペースを
ほぼ線形にシフト、**マーク**は復調長を約1キャリア周期刻みで動かす（サイクル計数）。
IRremoteESP8266のゼロ空白上限は送る値に依らず **`(526−50)×1.25 ≈ 595µs` 固定**なので、
受信ゼロ空白を下げれば余裕が増える。2Dで応答曲面（多くは縞模様）と最頑健点が見える。

## 実行

```sh
cd tests
uv run --env-file .env pytest -s -o python_files="study_*.py" studies/jvc_timing_sweep/
```

環境変数：`JS_MARKS`(`500,510,…,560`)、`JS_ZSPACES`(`470,490,…,550`)、
`JS_ONE`(`1575`、JVC標準で固定)、`JS_N`(`12`本/セル)、`JS_BITS`(`c0de`)、
`JS_CSV`(`data/jvc_sweep.csv`)。各セル1行＋最良セルを表示し、CSVを書き出す。

## 解析

```sh
uv run --with matplotlib python studies/jvc_timing_sweep/analyze.py \
  studies/jvc_timing_sweep/data/jvc_sweep.csv
```

2つのヒートマップ（行=ゼロ空白、列=マーク）：`margin_p90`（595 − p90受信ゼロ空白、
高い＝余裕大＝最適）と `pass_ratio`。

## 観測結果

7マーク × 5ゼロ空白、N=12、one-space 1575、bits 0xC0DE、IRremoteESP8266経由。

```
pass_ratio   行=ゼロ空白us, 列=ビットマークus
zsp\mark  500   510   520   530   540   550   560
  470    0.42  0.67  0.75  1.00  1.00  1.00  1.00
  490    0.58  0.83  0.50  1.00  1.00  1.00  1.00
  510    0.33  0.75  0.75  0.92  0.92  1.00  1.00
  530    0.00  0.25  0.17  0.33  0.33  0.42  0.42   <- JVC既定 (530/530)
  550    0.00  0.00  0.00  0.00  0.00  0.00  0.00
```

両軸が効く：

- **ゼロ空白（支配的）**：下げると受信ゼロ空白がほぼ線形に下がる。594余裕は
  zspace 470/490/510/530/550 で +75/+49/+23/−3/−6µs。550では全滅。
- **マーク（二次・実在）**：同じ行でmarkを上げるとpass上昇（zspace530: mark500→0.00,
  mark560→0.42、`zero_max` も ~622→~602µs に縮む）。carrier_loopback のサイクル効果の
  確認＝markが長い→サイクル21寄り→復調マーク長め→最悪スペース短め。
  （注：`margin_p90` ヒートマップはmarkでフラットに見えるが、デコード合否は**最悪ビット**で
  決まるのでmark効果は`pass_ratio`/`zero_max`に出る。p90には出ない。）

**最頑健ゾーン：mark ≥ 530µs かつ zero-space ≤ 490µs → pass 1.00。** JVC既定
（530/530＝525の丸め）は0.33。緩和策候補は例 mark≈540 / zero-space≈480（JVC tol内）。
ただし下の注意のとおり1受信機向けチューニングなので要クロスチェック。

## 注意

- **受信機別チューニング/過適合**：IRremoteESP8266に最適化すると Arduino-IRremote や
  実機リモコンで悪化しうる。採用前に最良セルを他受信機（`compat_matrix/`）と当方RMT RXで再確認。
- **キャリア位相の床**：フリーランキャリアの±1周期残留があり、どの (mark, zspace) でも
  完全決定化はできない（`carrier_loopback/` 参照）。本リグは最良の**緩和策**を見つけるもので、
  根本対策ではない。
- 各デコーダのtol窓内に収めること。JVC標準(525/525)から外す＝環境別の選択なので明記する。
