# 信号fixture

テストで使うIR信号データをここに置きます。

## 種類

- `generated/`: protocol specとBITSから生成した理想波形
- `verified/`: 手書きまたはレビュー済みの固定RAW
- `captured/`: 市販リモコンや実機テストで採取した未整理RAW

## 基本形式

RAWはライブラリ仕様に合わせて `10us = 1 tick` の `raw_ticks` で保存します。レビューしやすい場合は `raw_us` を併記しても構いません。

例:

```yaml
name: nec_power
protocol: NEC
bit_length: 32
bits: 0xcb3400ff
raw_ticks:
  - 900
  - 450
  - 56
  - 56
expected:
  address: 0x00ff
  command: 0x34
```

## 使い分け

- `protocol + bits` 送信: `IRSender` と `IRReceiver` の統合経路を見る
- `raw_ticks` 送信: 既知波形に対するdecodeを見る
- `captured` から `verified` への昇格: 手動レビュー後に行う
