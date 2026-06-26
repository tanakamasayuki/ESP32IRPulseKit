# IRダンプ（手動）

> English: [README.md](README.md)

合否を出すテストではなく、**手でリモコンを向けて使うIRダンプツール**です。受信系の
examplesをGPIO直書きで動かす手間を省くためにあります。`tests/.env` からシリアル
ポートとRX GPIOを読み、1本にまとめたダンプスケッチを焼いて、受信したフレームを
すべてコンソールに出力します。リモコンを受信機に向けて出力を読むだけ。**Ctrl-Cで
停止**します。

受信系のexamples 3本を1スケッチに統合しているので、**汎用リモコンとエアコンの
リモコンを同時に**扱えます:

- `examples/01_rx_dump` — RAW波形 + 全デコード候補 + プロトコル別の名前付き
  フィールド（NECのaddr/cmd、Sonyのdata など）。
- `examples/06_ac_learn` — RAWキャプチャに対するACベンダデコード
  （Panasonic / Gree / Mitsubishi）をコメントとして出力。
- `examples/04_learn` — 再送用の貼り付け可能なC++（デコード済みスニペット +
  RAW再送スニペット）。

## 出力例

受信フレームごとに:

```text
==== IR DUMP ====
raw.len(ticks)=68 flags=0x0
raw(us): 9000 4500 560 560 560 1690 ...
-- decoded candidates --
#0 pid=1 protocol=NEC score=100 len=32 bits=0xCB3400FF frame_type=NORMAL
  frame: NEC addr=0x0 cmd=0x34
-- AC vendor decode --
// decoded: no AC vendor matched (raw replay still works)
-- send code --
// send code (decoded):
esp32irpk::IRDecodedBits bits{};
...
// send code (raw replay):
const uint16_t ticks[] = {900, 450, 56, ...};
tx.send({ticks, 68});
```

エアコンのリモコンなら AC行が埋まります。例:
`// decoded: Panasonic AC  power=on mode=3 temp=24C fan=2  checksum=ok`。

## 実行

`tests/` から:

```sh
# 既定: ビルド+アップロードしてダンプを表示
uv run python studies/dump/dump.py

# 焼かずに、焼き込み済みボードへ接続するだけ
uv run python studies/dump/dump.py --no-flash

# 汎用のみで反応を速くしたい場合（idleを短く）。1回のAC押下が複数の
# "==== IR DUMP ====" に割れる場合は逆に上げる
uv run python studies/dump/dump.py --idle-us 35000
```

古い／不一致のファームに騙されないよう、焼き込みが既定です。

ポートとGPIOは `tests/.env`
（`TEST_SERIAL_PORT_RX_ESP32S3`, `TEST_IR_RX_GPIO`, `TEST_IR_RX_INVERTED`）から
読みます — `studies/link_quality` と同じキーです。実行ごとに `--port`, `--gpio`,
`--inverted`, `--idle-us`, `--max-symbols`, `--profile` で上書きできます。

## 統合で両立できる理由

`01_rx_dump` は候補から汎用プロトコルをデコードし、`06_ac_learn` はRAW
キャプチャからACをデコードします。ダンプスケッチはデコード候補を**有効のまま**にし
（汎用）、同時にRAW経路にAC向けの容量（`IR_RX_MAX_SYMBOLS`）と長いidle
（`IR_RX_IDLE_US`、既定100ms）を与え、複数フレームのACバーストを丸ごと捕えて
ACベンダに当てます。唯一のトレードオフは、長いidleが汎用のリピートフレームを1
キャプチャにまとめてしまうこと — ダンプ用途では問題ありません。

## 構成

```text
dump.ino          統合RXダンプスケッチ（出力整形は全てここ）
dump.py           ホスト側ランナー: .envから焼いてシリアルを垂れ流す
sketch.yaml       arduino-cliプロファイル（rx_esp32s3）
build_config.toml env→defineマッピング（pytest-embedded用。dump.pyは同じ
                  defineを --build-property で直接渡す）
```
