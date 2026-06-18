# hardware compat matrix

> English: [README.md](README.md)

`compat_matrix/` は外部ライブラリや別実装との互換性・差分調査用です。自前TX -> 自前RXの複数protocol確認は `hardware/protocol_matrix/` で扱います。

pytest-embedded のpeer構成に合わせ、各テストディレクトリでは親sketchをRX、`peer_tx/` をTXに固定します。peer名を `tx` に固定することで、ポート指定は `TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3` を共通利用できます。

バリアント:

```text
irremoteesp8266_tx/     # RX: ESP32IRPulseKit, TX: IRremoteESP8266
irremoteesp8266_rx/     # RX: IRremoteESP8266, TX: ESP32IRPulseKit
arduino_irremote_tx/    # RX: ESP32IRPulseKit, TX: Arduino-IRremote
arduino_irremote_rx/    # RX: Arduino-IRremote, TX: ESP32IRPulseKit
```

4バリアントすべて同じ構成です。`*_tx` は ESP32IRPulseKit のRX sketchを残して
`peer_tx/` の送信側を外部ライブラリに差し替え、`*_rx` は ESP32IRPulseKit の
`peer_tx/` 送信側を残して親の受信側を外部ライブラリに差し替えます。各テストは
観測bitsに加えて `bit_order`（same / reversed / other）を記録します。実装間の差は
MSB/LSB-first の整数表現の違いだけ、というケースが多いためです。

`compat_matrix` は任意実行です。score、raw_len、decode結果、raw timingのばらつきを観測し、bit orderやfield解釈の差分を記録する目的で使います。

## プロトコルカバレッジ方針

PulseKitの `protocol_matrix` は自前TX -> 自前RXで14プロトコルを確認します。
`compat_matrix` は外部ライブラリとの共通範囲と仕様差を調べるため、外部側の標準decode/send
があるものを優先して追加します。外部側に標準対応が無いものは、テストで失敗してよいですが、
READMEに理由と観測ログを残します。

2026-06-18時点でローカルに入っているライブラリ実装を見た整理:

| PulseKit protocol | Arduino-IRremote 4.7.1 | IRremoteESP8266 2.9.0 | compat方針 |
|---|---|---|---|
| NEC | 送受信あり | 送受信あり | 通常互換対象 |
| SONY12 | 送受信あり | 送受信あり | 通常互換対象 |
| SONY15 | 送受信あり | 送受信あり | 通常互換対象 |
| SONY20 | 送受信あり | 送受信あり | 通常互換対象 |
| SAMSUNG32 | 送受信あり | 送受信あり | 通常互換対象 |
| SAMSUNG36 | 専用decodeなし。raw送信APIでは36bit送信可能 | `sendSamsung36()` / `decodeSamsung36()` あり | IRremoteESP8266互換は修正候補。Arduino-IRremote RXは対応範囲外 |
| JVC | 送受信あり（16bit） | 送受信あり（16bit） | 通常互換対象 |
| AEHA | Kaseikyo系あり。PulseKit AEHAとの対応関係は未整理 | Panasonic/Kaseikyo系あり。PulseKit AEHAとの対応関係は未整理 | 次の調査候補 |
| PANASONIC40 | Kaseikyo/Panasonic系あり。40bit形は未確認 | Panasonic/Kaseikyo系あり。40bit形は未確認 | 次の調査候補 |
| PANASONIC48 | Kaseikyo/Panasonic系あり | Panasonic/Kaseikyo系あり | 次の追加候補 |
| RC5 | 送受信あり | 送受信あり | 次の追加候補 |
| RC6_M0_16 | RC6系あり | RC6系あり | 次の追加候補。ただし表現bit長差に注意 |
| RC6_M6_32 | RC6A/RC6系あり | RC6系あり | 調査候補。mode 6A表現差に注意 |

優先順位:

1. `SAMSUNG36` をIRremoteESP8266の二分割Samsung36波形に合わせるか検討する。
2. `PANASONIC48` / `RC5` / `RC6_M0_16` をcompat対象に追加できるか、外部APIの値表現を確認する。
3. `AEHA` / `PANASONIC40` / `RC6_M6_32` は、外部ライブラリのprotocol名・bit構造とPulseKitの仕様が対応するかを先に調査する。

## 現時点の所見と仮説（NEC、2026-06-18時点）

**TX↔RX を極端に近づけた（<10cm）**状態でNECを流すと、4方向のうち2方向が失敗、
残り2方向は成功します。

| 方向 | 結果 | 受信側が見る値 |
|---|---|---|
| IRremoteESP8266(50% duty) → 当方RX | ✅ 成功 | ゼロ空白 ~590us |
| 当方TX → Arduino-IRremote RX | ✅ 成功 | — |
| **Arduino-IRremote(30% duty) → 当方RX** | ❌ 失敗 | ゼロ空白が **~780us** に膨張（当方の700us上限超え） |
| **当方TX → IRremoteESP8266 RX** | ❌ 失敗 | ゼロ空白 ~594–672us（相手の~637us上限超え） |

### 仮説：失敗の正体は近接時のTSOP復調**バイアス**（ジッターではない）

近接ではTSOP復調器が**飽和**し、マークの立ち下がりを早めに切る。保存関係で、
マークから削れた時間が直後のスペースに乗る。結果は**系統的オフセット**：マークは
~80–90us *短く*、ゼロ空白は ~80–90us *長く* 受信される。通常距離のTSOPは逆
（マーク長・スペース短）で、各ライブラリの ~50us「マーク超過」補正はこれを前提に
している。近接飽和は**逆方向にずれる**ため、補正がかえって裏目に出てゼロ空白が
復調許容を越える。

- **失敗A（当方RX）**：Arduinoの30%デューティ信号でゼロ空白が ~780us に膨張し、
  当方の 560±25%＝700us を超過。当方デコーダは空白が 560 か 1690 の±25%に入る
  ことを要求するため、780 はどちらの窓にも入らず脱落。
- **失敗B（IRremoteESP8266 RX）**：相手は（通常方向を前提に）希望スペースから
  50us を**差し引く**ため、ゼロ空白上限が ~637us に**逆方向へ**狭まる。当方の膨らんだ
  ~650/672us が弾かれる。TXデューティを50%に上げても直らない（672まで跳ねる）。

### なぜジッターテストではほとんど差が出なかったか

`tx_jitter_loopback` / `tx_jitter` / `carrier_jitter` が測っていたのはフレーム間の
**ばらつき（標準偏差、~5–30us）**。今回のバイアスは別の軸：

- loopback は**有線（TSOPを通らない）**ので復調バイアスは皆無。RMT固有の決定性だけ。
- carrier/jitter 系は **sd** を報告し、一定の平均オフセットは正規化されていた
  （見出しは「整数キャリアサイクルで sd の谷」）。
- 今回も**ばらつきは ~20us** と同程度。デコードを壊しているのは ~90us の**平均
  バイアス**で、これは空中伝送の*デコード互換*経路でしか表面化しない。

### ステータス／次の一手

- これは仮説で、**現実的な距離での再測定**待ち。通常距離ならTSOPが正常動作し、
  失敗2方向とも通る見込み。失敗Bを直せるのは距離だけ（IRremoteESP8266のデコーダは
  変えられない）。
- `hardware/link_quality/`（手動メーター）で、ゼロ空白バイアスが小さく外部RXの
  「compat margin」が正になる配置を探せる。
- ライブラリ側の堅牢化案（失敗Aのみ解消）：NECビット判定を厳密な±25%帰属では
  なく「最近傍の期待スペース（閾値≈1125us）＋緩いマークチェック」にする（多くの
  NECデコーダの方式）。未実装。コミット済みの33%デフォルトduty決定には触れない。

### JVC → IRremoteESP8266：根本原因を特定

JVC（当方TX→IRremoteESP8266 RX）は ~1/5 と際どいが、同じ配置で
`irremoteesp8266_self` は JVC を 5/5 でデコードする → **環境ではなく送信側**の問題。
TXデューティを50%にしても直らない。`hardware/carrier_loopback/`（TSOPなし・1µs
RMTで生キャリアを捕捉）で確定：キャリア周期は綺麗だが、同一530µsマークが
**20 or 21 サイクルをほぼ半々のコインフリップ**で持つ（フリーランキャリア位相）。
この±1周期（~26µs）が復調マーク/スペースをずらし、ゼロ空白の一部が
IRremoteESP8266の狭い~594µs JVC窓を越える。データと対策候補（マーク毎のキャリア
位相整合、おそらく~1µs解像度でのシンボルエンコードキャリア）は
[carrier_loopback/README.md](../carrier_loopback/README.md) 参照。
