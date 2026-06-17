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

各バリアントが NEC を50回送信し、`JITTER_LOOPBACK_OBSERVED` にエッジごとの
`mean_stdev_us` / `max_stdev_us` / `max_ptp_us` を出力します。IRリンクとキャリアを
除いているため、ここでの差は送信側の生のタイミング精度（RMTハード vs
タイマー/delay生成）を反映します。
