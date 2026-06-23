# 位相整合キャリア（実験）

> English: [README.md](README.md)

DESIGN §12 の送信キャリア位相問題に対するA/B実験です。

通常ライブラリはフリーランニングのハードウェアキャリア（`rmt_apply_carrier`）で
送信します。マーク毎に位相がリセットされないため、マークがランダムな位相で終わり、
復調マーク幅が最大±1キャリア周期（38kHzで~26µs）ぶれます。本studyは、**実験的な
位相整合パス**（`IRSender::setPhaseAlignedCarrier(true)`）でこのぶれが消えるかを測定
します。位相整合パスはRMTチャネルを1µsで動かし、キャリアを位相整合シンボル（マーク毎に
整数個の完全サイクル）として符号化するので、各マークが必ず位相0から始まります。

- `peer_tx/`（送信）: ライブラリ `IRSender` でNEC形状のRAWフレームを送信。コマンドで
  `hw`（ハードウェアキャリア）/ `pa`（位相整合）を切替。
- `phase_aligned_carrier.ino`（受信dut）: 1µsで捕捉し `RX_JITTER` を出力。
- `study_phase_aligned_carrier.py`: 各モードで多数フレームを送り、フレーム間のマーク
  標準偏差 / peak-to-peak を比較。

どちらのスケッチも仕様準拠のライブラリ利用で、実験はキャリア生成方式だけを変えます。

## 実行

自動収集されません（`study_*.py`）。2台治具で必要時に実行します。

```sh
uv run --env-file .env pytest -s -o python_files="study_*.py" studies/phase_aligned_carrier/
```

`PHASE_ALIGNED_COMPARE mark=… hw_sd=… pa_sd=…` 行を読みます。`pa_sd` / `pa_ptp` が
`hw_*` より明確に小さければ、位相整合パスがぶれを解消しており、既定キャリアパスへの
採用候補になります（JVCタイミング調整の撤去・AEHA→IRremoteESP8266 の改善にもつながる）。
スイープは `PA_MARKS` / `PA_CARRIER_HZ` / `PA_DUTY` / `PA_FRAMES` で調整します。

## RMTメモリブロック

位相整合パスはキャリア1サイクル≒1シンボルで送るため、フレーム中ずっとドライバが
チャネルメモリを継ぎ足します。ブロック数（`IRSender::setTxMemBlocks(n)`、1ブロック=
`SOC_RMT_MEM_WORDS_PER_CHANNEL`＝ESP32/S2で64・他で48）が継ぎ足し余裕を決めます。
大きいほど安全側だが共有RMT TXメモリプールを消費します。peer は `BLOCKS <n>` コマンドを
受け付ける（0=ライブラリ既定=位相整合で2ブロック）ので、既定値を決める前に「何ブロックまで
減らしてもきれいに送れるか」を実機で詰められます。
