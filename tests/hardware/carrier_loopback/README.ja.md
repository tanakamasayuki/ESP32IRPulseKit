# キャリア ループバック プローブ（1ボード）

> English: [README.md](README.md)

TSOPを経路から外し、**ライブラリが出すキャリアそのもの**を直接観測するリグです。
PulseKit→IRremoteESP8266 の JVC が際どい原因を確定させるのが目的。1ボードが
**キャリア変調されたマーク**の短いパターンをライブラリTXで送信し、**有線ループ
バックした電気信号を 1µs（RMT、復調器なし）で捕捉**するので、キャリアの半周期
ごとに別エッジとして見えます。

配線：**`LOOPBACK_TX_GPIO` → `LOOPBACK_RX_GPIO`** を直結（既定 GPIO5 → GPIO6、
`.env` 反映）。IR LEDもTSOPも使いません。

## 何を切り分けるか

JVCのジッターは送信側に帰属済み。TX側の候補メカニズムは2つあり、このリグで分離します：

- **キャリア周期の量子化** — ライブラリの100kHz（10µs）RMT解像度では 38kHz は
  2.63 tick/周期で割り切れず、周期がディザる可能性。→ **マーク内の周期ばらつき**が大。
- **キャリアのフリーラン位相** — RMTキャリアは連続発振なので、マーク境界での位相が
  マーク毎にドリフトする。→ 同じ幅のマークでも**キャリアON期間/サイクル数がマーク毎に変動**。

## 実行

```sh
cd tests
uv run --env-file .env pytest -s hardware/carrier_loopback/
```

`CAP <mark_us> <space_us> <count> [duty_pct] [carrier_hz]` で `count` 本のキャリア
マークを送り、生1µsキャプチャをダンプ。環境変数で上書き：
`CL_MARK`(530)/`CL_SPACE`(530)/`CL_COUNT`(4)/`CL_DUTY`(33)/`CL_HZ`(38000)/
`CL_SENDS`(20)/`CL_OUT`(`data/carrier_loopback.txt`)。

パターンは短く：キャリアONだと26µsごとに1エッジ対が出て、捕捉は
`RMT_MEM_NUM_BLOCKS_4`（約192シンボル）が上限。4×530µs（約80サイクル）は安全。
フルフレームは溢れる。

## 出力 / 解析

テストはサマリを表示し、生ダンプを `data/` に保存。オフライン再解析（プロット）：

```sh
uv run --with matplotlib python hardware/carrier_loopback/analyze.py \
  hardware/carrier_loopback/data/carrier_loopback.txt --plot out.png
```

主な項目：

- `carrier period: mean .. within-mark sd ..` — 周期の綺麗さ。sdが大（>~2µs）なら
  **TX解像度**ディザが疑わしい。
- `cycles/mark` と `carrier-on span sd` — マーク毎のばらつき。非ゼロなら
  **フリーランキャリア**（位相ドリフト）が疑わしい。

判定が根本対応の指針になります：周期量子化ならTX解像度を上げれば効く。位相ドリフト
なら**マーク毎のキャリア位相リセット**が必要。
