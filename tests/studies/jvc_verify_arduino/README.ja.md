# JVC チューニング値クロスチェック（Arduino-IRremote RX）

> English: [README.md](README.md)

[../jvc_timing_sweep/](../jvc_timing_sweep/) の最良値の過適合チェック。スイープは
JVCのマーク/ゼロ空白を IRremoteESP8266 向けに最適化した。ここでは同じ可変JVC
（`JVCRAW` peer、jvc_timing_sweep と同一）を**別の独立受信機 Arduino-IRremote** に
送り、JVC標準とチューニング候補を比較して、調整が1デコーダへの過適合でないか確認する。

- **RX（jvc_verify_arduino.ino）**：Arduino-IRremote（`IrReceiver`）。成功時
  `RX_DECODE protocol=JVC<bits>`。
- **TX（peer_tx/）**：ESP32IRPulseKit `JVCRAW <mark> <zspace> <ospace> <hex>`。

```sh
cd tests
uv run --env-file .env pytest -s -o python_files="study_*.py" studies/jvc_verify_arduino/
```

## 観測結果

N=15/点、one-space 1575、bits 0xC0DE、2台IR。

| 点 | mark/zspace | IRremoteESP8266(スイープ) | Arduino-IRremote |
|---|---|---|---|
| std（JVC既定） | 530/530 | 0.33 | **15/15** |
| tuned（最良） | 540/480 | ~1.00 | **15/15** |
| tuned-lo | 540/470 | ~1.00 | 15/15 |
| low-mark（対照） | 520/530 | ~0.17 | 15/15 |

Arduino-IRremote は寛容で**全点100%**デコード（`JVC16`）。つまりチューニング値は
**厳しい受信機（IRremoteESP8266: 0.33→~1.00）を直しつつ Arduino では無劣化**、
当方RX（ゼロ空白700µs窓）も両方通る＝**過適合ではない**。

注意：Arduinoは窓が広いので「窓内に収まる」確認であって厳密なストレステストではない。
実機JVCは試せない。またtunedは**マージンで吸収**しているだけで、フリーランキャリア
ジッター（[../carrier_loopback/](../carrier_loopback/)）自体は消していない＝実用的な
緩和策。標準(525/525)から外す採用判断は明記すること。
