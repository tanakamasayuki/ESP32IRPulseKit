# リンク品質メーター（手動）

> English: [README.md](README.md)

合否を出すテストではなく、**手でボードを動かしながら使うIR環境メーター**です。
TXボードがNEC固定フレームを連続送信し、RXボードが復調、ホストが**1行をその場
更新**してリンク品質をスコア表示します。2台を動かして（距離・角度・遮蔽）スコアと
判定がリアルタイムに変わるのを見ながら、良い配置を探します。**Ctrl-Cで停止**、
最後にサマリを表示します。

背景：compat_matrix のNEC失敗は、**幾何条件に依存する受信側の復調アーティファクト**
でした。近接しすぎるとTSOPが飽和し「マーク短い／スペース膨張」（通常距離とは逆）に
報告され、ゼロ空白が復調許容を超えます。特に外部受信機の狭い窓で顕著です。この
メーターで、手探りで良い物理配置を見つけられます。

## 表示内容

```
[GOOD                       ] decode 100%(30/30)  recv 100%  mark 561(+1)sd5  sp0 590(+30)sd11  sp1 1702  compat-margin(JVC) +39us  score 86
[TOO CLOSE (saturated)      ] decode  70%(21/30)  recv 100%  mark 521(-39)sd9 sp0 712(+152)sd24 sp1 1840 compat-margin(JVC) -83us  score 36
```

- **verdict（判定）** — `GOOD` / `TOO CLOSE (saturated)＝近すぎ飽和` /
  `TOO FAR / WEAK＝遠すぎ/微弱` / `MARGINAL＝外部RXが弾く恐れ` /
  `UNSTABLE＝ジッター大`。
- **decode %** — 当方デコーダの成功率（直近ウィンドウ）。
- **recv %** — 何らかのRX行が出たフレーム率（取りこぼし＝遠すぎ）。
- **mark / sp0 / sp1** — 受信幅の平均(us)とNEC公称(560/560/1690)との符号付き差。
  `sd`はジッター。マーク短＋sp0膨張が近接飽和のサイン。
- **compat-margin(JVC)** — **最も厳しい外部受信機**に対する最悪値(p90)の余裕(us)。
  基準は **IRremoteESP8266 の JVC**：ゼロ空白上限が `(525−50)×1.25 ≈ 594us` で、
  NECの ~638us より狭い。メーターはNECを送信し、復調のゼロ空白**膨張量**
  （≒マーク欠損、ほぼ一定の絶対オーバー分）を測り、それを JVC の 525us 公称へ
  投影して算出する。負なら、当方デコーダは通っても外部受信機は落とす可能性が高い。
  JVC基準にしているので、**「GOOD」＝NECだけでなく全内蔵プロトコルが外部でデコード
  される見込み**を意味する。
- **score 0–100** — `60·decode率 + 25·compat余裕項 + 15·安定度`。

読み方：スコアを高く、かつ compat-margin を正に。判定が `TOO CLOSE` なら離す、
`TOO FAR / WEAK` なら近づける／角度・遮蔽を減らす。

## 実行

`tests/` から：

```sh
# 既定: 両ボードをビルド＋書き込みしてから計測
uv run python studies/link_quality/monitor.py

# 書き込みを省略して、書き込み済みボードに接続
uv run python studies/link_quality/monitor.py --no-flash
```

書き込みを既定にしているのは意図的：再書き込みは安いが、古い／不一致のファームを
黙って計測する方が怖い。毎回転送する方が安全。

オプション: `--window N`（直近フレーム数、既定30）、`--interval S`（送信間隔秒、
既定0.2）、`--bits HEX`（NECペイロード、既定 `cb3400ff`）、`--no-color`。

ポートとGPIOは `tests/.env` から取得します
（`TEST_SERIAL_PORT_RX_ESP32S3`, `TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3`,
`TEST_IR_RX_GPIO/INVERTED`, `TEST_IR_TX_GPIO/INVERTED`）。

## 構成

```text
rx/        ESP32IRPulseKit 受信。RX_DECODE/RX_RAW と生tickを出力
tx/        ESP32IRPulseKit 送信。"SEND NEC <hex>" でNEC送信
monitor.py ホスト側ライブダッシュボード＋スコア計算（指標ロジックは全部ここ）
```
