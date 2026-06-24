# 実機Compat Matrix（エアコン）

> English: [README.md](README.md)

`compat_matrix_ac/` は ESP32IRPulseKit のエアコン層（`esp32irpk::ac`）を外部AC
ライブラリと比較します。`compat_matrix/` とは**軸が違う**ため別フォルダにします。
汎用matrixは protocol の **bit / timing / bit-order** を比較しますが、こちらは
デコードした **vendor状態フィールド**（power / mode / temperature / fan）を比較
します。

主目的は `src/ac/Panasonic.h` の **provisional な Panasonic フィールドマップ**
（各フィールドのバイト/ビット位置、ベンダ別 mode/fan コード）の**較正**です。
フレーム機構（Kaseikyo 2フレーム、timing、総和チェックサム）は公開仕様から実装
済みで、この治具は実機リモコンが無くても独立リファレンスとフィールドマップを
突き合わせて確認します。

## 参照ライブラリ

| ライブラリ | 方向 | 役割 |
|---|---|---|
| IRremoteESP8266（`IRPanasonicAc` / `IRac`） | decode + encode | 双方向クロスチェック |
| HeatpumpIR（`PanasonicHeatpumpIR`） | 送信のみ | 2系統目の独立TXリファレンス |

独立した2つのエンコーダが一致し、当方デコーダもそれに一致すれば、実機リモコンが
無くてもフィールドマップを確定できます。

## バリアント

`compat_matrix/` と同じ `<extlib>_<role>` 命名。主sketchがDUT、`peer_tx/` が
送信側で、peer名は `tx` 固定（`TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3` を使い回す）。

```text
irremoteesp8266_tx/    # TX: IRremoteESP8266（既知状態） -> RX: ESP32IRPulseKit  （当方decode較正）
irremoteesp8266_rx/    # TX: ESP32IRPulseKit -> RX: IRremoteESP8266              （当方encode検証）
irremoteesp8266_self/  # TX + RX: IRremoteESP8266                               （参照ベースライン）
heatpumpir_tx/         # TX: HeatpumpIR（既知状態） -> RX: ESP32IRPulseKit       （2系統目の参照）
```

`irremoteesp8266_self`（IRremoteESP8266 が既知状態をエンコードし、自分の送信を
デコード）はベースライン。クロス方向を信頼する前に、参照が物理治具/配置で往復
できることを確認します。

Arduino-IRremote はここでは**使いません**。エアコン状態のデコーダを持たず
（Panasonic を汎用48bitフレームとしてしか読まず、AC状態は復号しない）、フィールド
比較ができないためです。

- `*_tx`（外部が送信、当方RX）: peer が**既知**のAC状態（例: 冷房/26℃/風量自動）を
  送信。当方RXがRAWをキャプチャ（RAWのみ＋`setMaxRxSymbols`＋大きめidle）し
  `ac::Panasonic::Frame::fromRaw` でデコードしてフィールドを出力。study は
  `送信状態 → 当方デコード結果` を記録＝これがフィールドマップ確定の材料。
- `irremoteesp8266_rx`（当方が送信、外部RX）: 当方 `ac::Panasonic` で状態を
  エンコードし、IRremoteESP8266 が復号。当方エンコーダの検証。

## シリアル形式

ESP32IRPulseKit RX 主sketch は受信ACバーストごとに1行出力します。

```text
AC_DECODE vendor=PANASONIC checksum=ok power=1 mode=1 temp=26 fan=0 bytes=0220e0...
```

- `mode` / `fan` は `ac::Panasonic::Mode` / `Fan` の生の underlying 値（名前化
  ヘルパーは後付け。SPEC §11.2）。study は送信した既知状態を、当方デコーダが
  返した値に対応づけます。
- `checksum` は `Frame::checksum_ok` の `ok` / `bad`。
- `bytes` は27バイトの状態全体のhex（配置を人が差分確認できるよう）。

外部RX sketch は参照ライブラリ自身のデコード（vendor＋フィールド）を同じ
`AC_DECODE` 形式で出力します。

## 実行

実機study。自動収集されません（`study_*.py`）。ESP32-S3 2台と `.env` のローカル
Serialポート/GPIOが必要です。

```sh
uv run --env-file .env pytest -s -o python_files="study_*.py" \
  studies/compat_matrix_ac/irremoteesp8266_tx/
```

## 状態

設計とシリアル契約はここで確定。バリアント別のsketchとハーネスは1つずつ追加。
実装済み:

- `irremoteesp8266_self/` — ベースライン: 物理治具がPanasonic ACフレームを往復
  できること（IRremoteESP8266 がエンコード→送信→受信→同じ27バイトへデコード）を、
  クロス方向を信頼する前に確認。
- `irremoteesp8266_tx/` — フィールドマップ較正: IRremoteESP8266 が既知状態を送信、
  当方RXがRAWキャプチャして `esp32irpk::ac::Panasonic` でデコード。正準フレームとの
  バイト一致が合否判定（hard）、既知状態とのフィールド単位比較は報告のみ（assertし
  ない）でフィールドマップ確定の材料にする。
- `irremoteesp8266_rx/` — encoder検証（`_tx`の対）: 当方TXが `esp32irpk::ac::Panasonic`
  で既知状態をエンコードして送信、IRremoteESP8266 がデコード。外部ライブラリが復元
  したバイトが当方encoderの生成バイトと一致（checksum ok）すること＝当方 `toRaw()` が
  独立スタックも受理する正しいバーストを出すことの証明。

続いて `heatpumpir_tx/`。所見と確定したフィールドマップは
ここと `src/ac/Panasonic.h` に反映します。

`irremoteesp8266_tx/` の結果、`src/ac/Panasonic.h` のPanasonicフィールドマップが
IRremoteESP8266 の `IRPanasonicAc` とバイト単位で一致することを確認: 当方のRAW
キャプチャが正準27バイトを再現し、power / mode（auto/cool/heat/dry）/ temperature
/ fan がすべて期待値にデコードされる。fan nibble は Panasonic の速度＋3
（min/low/med/high/max = 0x3〜0x7、auto = 0xA）。

`irremoteesp8266_rx/` の結果、当方encoderも確認: setterで構築し `toRaw()` で描画した
フレームが、実機リモコンが常に持つ固定feature byte（[15]=0x80, [19]=0x0E, [20]=0xE0,
[23]=0x81）を含む完全な正準状態になる。そのため `Frame` は既知良テンプレートを既定値
とし、mode/temp/fan/power だけ設定してもこれらのバイトが揃う。

### キャリア信頼性（長尺TX）

受信側（IRremoteESP8266）もフレームも同一で、2回の run で送信側だけが違う比較:
IRremoteESP8266 純正TXは 25/25 到達、当方TXは自走ハードウェアキャリア
（`setPhaseAlignedCarrier(false)`、この長さのフレームには必須 — SPEC §11.3）で
23/25。到達したフレームはすべてバイト正確（破損ゼロ）で、2件は丸ごとのドロップ。
この差は、長尺フレームではハードウェアキャリアが位相整合キャリアよりわずかに
検出されにくいことを示す。デューティや連送による緩和はここでは対象外。長尺向けの
位相整合（またはライブエンコード）キャリア対応は今後の課題。
