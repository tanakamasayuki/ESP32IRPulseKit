# TX ジッター

> English: [README.md](README.md)

各IRライブラリの**送信タイミング安定性（ジッター）**を測定します。固定フレーム
（NEC `0xCB3400FF`）を多数回送信し、高分解能の受信機で全エッジをキャプチャします。

受信機は ESP32IRPulseKit のライブラリRX（10us/tick固定）**ではありません**。各
バリアントの親sketchが ESP32 RMT を直接 **1MHz（1us/tick）** で駆動し、捕捉した
全エッジ長をマイクロ秒で出力します（`RX_JITTER seq=.. len=.. us=..`）。RMT捕捉は
ハードウェアタイミングで非常に安定しているため、繰り返し間のばらつきは
受信側ではなく**送信側**の特性を反映します。

## バリアント

```text
pulsekit/          # TX: ESP32IRPulseKit (RMT)
irremoteesp8266/   # TX: IRremoteESP8266
arduino_irremote/  # TX: Arduino-IRremote
```

各ディレクトリは 1us RMT RX を親sketchとし、`peer_tx/` は
`hardware/compat_matrix/` の対応する送信機を再利用します。

## 実行

```sh
cd tests
uv run --env-file .env pytest hardware/tx_jitter/
```

各バリアントで NEC を50回送信し、エッジindexごとに整列（正しいNECはエッジ数が
一定で、変わるのは各エッジ長のみ）して `JITTER_OBSERVED` に統計を出力します:

- `mean_stdev_us` — エッジごと標準偏差の平均
- `max_stdev_us` — 最悪のエッジ標準偏差
- `max_ptp_us` — 最悪のエッジ peak-to-peak（max − min）

期待: RMTベース送信（ESP32IRPulseKit）はジッター小、タイマー/delayベースの
ライブラリは大きく出る可能性。テストは「統計に十分な一定長フレームが取れたか」
のみをassertし、数値自体は観測ログとして記録します。
