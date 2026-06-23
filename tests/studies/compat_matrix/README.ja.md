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

PulseKitの `protocol_matrix` は自前TX -> 自前RXで11プロトコルを確認します。
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
| SAMSUNG36 | Samsung36 非対応 | `sendSamsung36()` / `decodeSamsung36()` あり | IRremoteESP8266 は通常互換対象。Arduino-IRremote は Samsung36 非対応のため対象外 |
| JVC | 送受信あり（16bit） | 送受信あり（16bit） | 通常互換対象 |
| AEHA | Kaseikyo系あり | Panasonic/Kaseikyo系あり | AEHA/Kaseikyo/Panasonic の正準デコーダ。IRremoteESP8266 クロステスト: tx成功、rxはマージナル（マーク幅、下記参照）、selfベースライン |
| RC5 | 送受信あり | 送受信あり | IRremoteESP8266 クロステスト（rx + tx） |
| RC6_M0_16 | RC6系あり | RC6系あり | IRremoteESP8266 クロステスト（rx + tx） |
| RC6_M6_32 | RC6A/RC6系あり | RC6系あり | 調査候補。mode 6A表現差に注意 |

優先順位:

1. `RC5` / `RC6_M0_16` を IRremoteESP8266 クロステスト（rx + tx）に追加。
   下記「RC5 / RC6 のバイフェーズ規約」参照。
2. `AEHA`（Kaseikyo/Panasonic）を IRremoteESP8266 クロステスト（rx + tx + self）に追加。
   下記「AEHA / Kaseikyo / Panasonic」参照。
3. `RC6_M6_32` を、外部ライブラリとの対応を先に調査する。

### RC5 / RC6 のバイフェーズ規約

RC5 と RC6 はバイフェーズ（Manchester）で、ハーフビット極性が **互いに逆**：

- **RC5**：`1` は space→mark、`0` は mark→space。先頭スタートビットの先頭スペースは
  アイドルギャップで、捕捉RAWには含まれない（RAWは最初のmarkから始まる）。
- **RC6**：`2666 / 889 µs` リーダーの後、`1` は mark→space、`0` は space→mark。
  スタートビットは単幅で、倍幅なのはトグル（4番目）ビットのみ。

整数表現は IRremoteESP8266 と異なる（PulseKit は start/mode/toggle を含めて RC5=14bit、
RC6 mode0=21bit と数え、IRremoteESP8266 はそれらを除いて 12-13 / 20bit）。よって
クロステストは値一致をassertせず `bit_order` を記録する。

### AEHA / Kaseikyo / Panasonic

`AEHA` が家製協ファミリの正準デコーダ。Kaseikyo（および特定メーカーコードを持つ
Panasonic）は 48bit の AEHA フレームなので、IRremoteESP8266 の Panasonic フレームは
PulseKit では `AEHA`（例 `0xBD3D802002`）として復号される。PulseKit の AEHA デコーダは
customer-code のパリティニブルを検証し、これが本物の家製協フレームの判別になる。

ビット順：PulseKit は LSB-first、IRremoteESP8266 は MSB-first 格納なので、同じ48bit波形でも
両者でビット反転して読める（Panasonic のメーカーコードは PulseKit 下位16bit で `0x2002`、
IRremoteESP8266 上位16bit で `0x4004`）。クロステストは同一の物理フレームを、各ライブラリ自身の表現で名付けて送る：tx/self は
IRremoteESP8266 の Panasonic 値 `0x40040100BCBD`（`sendPanasonic64`）、rx は PulseKit ネイティブ表現
`0xBD3D802002`（AEHA）= その波形を PulseKit が復号した値そのもの。

観測結果:

- **tx（IRremoteESP8266 → PulseKit）: 成功。** PulseKit が Panasonic フレームを
  `AEHA len=48 bits=0xBD3D802002`（score ~800）で復号。
- **rx（PulseKit → IRremoteESP8266）: マージナル／復号せず。** PulseKit は正しい AEHA 波形を
  送出し IRremoteESP8266 も RX_RAW で捕捉するが、Panasonic として復号されない。原因は
  ビット順でもチェックサムでもなく**マーク幅**（`decodePanasonic` は非strictでメーカー
  コード/チェックサムを見ない）。AEHA の仕様マークは 425 µs だが、PulseKit の RMT
  フリーランキャリアが切り詰め、受信側では平均 ~385 µs・約5%が ~348 µs 未満になる。
  IRremoteESP8266 の Panasonic は `432 + 50 µs` 付近のマークを期待し ~362 µs 未満を弾くため、
  48bit 全マーク一致が必要なフレームはほぼ毎回失敗する。DESIGN §12 のキャリア位相による
  マーク幅問題が別の短マークprotocolで顕在化した形。相互運用のために AEHA 仕様マークは
  短くしないので、これは記録された非互換のまま（NEC は 560 µs で窓が広く無影響）。
  ただし回避可能と確認済み: 実験的な位相整合キャリア（`PULSEKIT_CARRIER=pa`、
  `studies/phase_aligned_carrier` 参照）では rx peer のマークが窓内に収まり、
  IRremoteESP8266 が `PANASONIC48` として復号する（hw = 0/5、pa = pass）。
- **self（IRremoteESP8266 → IRremoteESP8266）: ベースライン**（環境が許せば。48bit フレームは
  本マトリクスで最も配置に敏感）。

フィールド/パリティ構造を含む専用 AEHA テストは将来追加。

### SAMSUNG36（2ブロック）

Samsung36 のフォーマットは **2ブロック**波形：header → 上位16bit → ブロック間セパレータ
スペース → 下位20bit を **MSB-first** で送り、bitタイミングは `512 / 1468 / 490 µs`、
header は `4515 / 4438 µs`。PulseKit はこのフォーマットに従い、専用パス
（`encodeSamsung36` / `decodeSamsung36`、RC5/RC6と同じ方式）で符号化/復号する。値は
MSB-first格納（`bits[35..20]`=address/ブロック1、`bits[19..0]`=command/ブロック2）。
同じく Samsung36 を実装する外部ライブラリ（IRremoteESP8266）に対しては、クロステストで
`bit_order = same` を観測する。Arduino-IRremote は Samsung36 非対応のため、本クロステスト
の対象外。

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
- `studies/link_quality/`（手動メーター）で、ゼロ空白バイアスが小さく外部RXの
  「compat margin」が正になる配置を探せる。
- ライブラリ側の堅牢化案（失敗Aのみ解消）：NECビット判定を厳密な±25%帰属では
  なく「最近傍の期待スペース（閾値≈1125us）＋緩いマークチェック」にする（多くの
  NECデコーダの方式）。未実装。コミット済みの33%デフォルトduty決定には触れない。

### JVC → IRremoteESP8266：根本原因を特定

JVC（当方TX→IRremoteESP8266 RX）は ~1/5 と際どいが、同じ配置で
`irremoteesp8266_self` は JVC を 5/5 でデコードする → **環境ではなく送信側**の問題。
TXデューティを50%にしても直らない。`studies/carrier_loopback/`（TSOPなし・1µs
RMTで生キャリアを捕捉）で確定：キャリア周期は綺麗だが、同一530µsマークが
**20 or 21 サイクルをほぼ半々のコインフリップ**で持つ（フリーランキャリア位相）。
この±1周期（~26µs）が復調マーク/スペースをずらし、ゼロ空白の一部が
IRremoteESP8266の狭い~594µs JVC窓を越える。データと対策候補（マーク毎のキャリア
位相整合、おそらく~1µs解像度でのシンボルエンコードキャリア）は
[carrier_loopback/README.md](../carrier_loopback/README.md) 参照。

**採用した緩和策:** ライブラリは JVC ゼロ空白を仕様の525µsではなく**480µsで送出**し、
受信ゼロ空白を594µs窓から離す（送信マージンの回避策で根本対策ではない）。実機検証済み：
当方TX→IRremoteESP8266 のJVCがデコード可に（従来~1/5）、Arduino-IRremoteは15/15維持
（[jvc_verify_arduino/](../jvc_verify_arduino/)）、当方RXは外部の標準525µs JVCもデコード可
（score~920）。詳細は `src/protocols/JVC.h` のコメント。
