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
# Panasonic
irremoteesp8266_tx/        # TX: IRremoteESP8266（既知状態） -> RX: ESP32IRPulseKit  （当方decode較正）
irremoteesp8266_rx/        # TX: ESP32IRPulseKit -> RX: IRremoteESP8266              （当方encode検証）
irremoteesp8266_self/      # TX + RX: IRremoteESP8266                               （参照ベースライン）
heatpumpir_tx/             # TX: HeatpumpIR（既知状態） -> RX: ESP32IRPulseKit       （2系統目の参照）

# Gree（esp32irpk::ac::Gree に合わせて IRGreeAC の YBOFB モデルを使用）
gree_irremoteesp8266_tx/   # TX: IRremoteESP8266（既知状態） -> RX: ESP32IRPulseKit  （当方decode較正）
gree_irremoteesp8266_rx/   # TX: ESP32IRPulseKit -> RX: IRremoteESP8266              （当方encode検証）
```

各ベンダは同じ `<extlib>_<role>` バリアントを使う。バリアントのフォルダ名は
ベンダ名で前置する（`gree_…`）。前置のないものは歴史的経緯で Panasonic。

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

- `heatpumpir_tx/` — 2系統目の独立参照: HeatpumpIR（`PanasonicJKEHeatpumpIR`、
  別コードベース、LEDCキャリア）が既知状態を送信、当方RXがデコード。hard判定は
  当方デコードが checksum 妥当かつ論理フィールドが送信状態と一致すること（バイト
  一致ではない — 独立した2つのencoderがフィールド意味で一致することに価値がある）。
  HeatpumpIR は fan段が1つずれるため、IRremoteESP8266 peer では届かない QUIET /
  POWERFUL も網羅する。

- `gree_irremoteesp8266_tx/` + `gree_irremoteesp8266_rx/` — Gree版の較正＋encoder
  検証の対。IRremoteESP8266 の `IRGreeAC` を **YBOFBモデル**で使う（モデルビットが
  常に0でbyte2が0x20固定となり `esp32irpk::ac::Gree` と一致）。`tx` で2ブロック
  （2ブロック目はヘッダ無し）の当方RAW復号が正準8バイトをバイト単位で再現すること、
  `rx` で当方encoderの出力を独立スタックが Kelvinatorブロックチェックサム妥当で
  受理することを確認。Gree特有の2点: (1) RX primary は終端タイムアウト50ms必須
  （`decodeGree` が約20msのブロック間ギャップを block2 のヘッダ空白として読むため、
  余裕が薄いと2ブロック目が切り離される）。(2) Gree は位相整合キャリアで送る必要が
  ある——自走ハードウェアキャリアの約1サイクルのマーク揺れでフレームの約半数が落ちる
  （zeroスペース540us がbitマーク620usより短く、揺れでスペースが許容を外れる。実測:
  位相整合 50/50 vs ハードウェア 約55%。`study_carrier_ab.py`）。位相整合でも540usの
  zeroスペースはスペースを伸ばす受光に対し余裕が小さいので、参照デコーダが既定許容で
  受理するにはIR経路がそれなりに整列している必要がある。

所見と確定したフィールドマップはここと `src/ac/Panasonic.h` / `src/ac/Gree.h` に
反映します。

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

ACのサンプルは `setPhaseAlignedCarrier(false)` で自走ハードウェアキャリアを選ぶ
（無指定ならライブラリのキャリアは位相整合）。位相整合はマーク精度が高くなり得るが、
長尺ACバーストを1送信あたり約17KBのシンボルに展開し、重い割り込み負荷下では
リフィルのアンダーランリスクが増す（SPEC §11.3）。そのためサンプルは長尺フレームで
ハードウェアキャリアを推奨する。

`study_carrier_ab.py` はその選択が到達率を損なうかを計測する。`irremoteesp8266_rx`
の peer（当方TX）はランタイム `CARRIER pa` / `CARRIER hw` コマンドを受け
（ビルド既定は `PULSEKIT_CARRIER`、0=ハードウェア）、各状態を両モードで送信して
モード別の正準到達率を記録する。実機治具では両キャリアとも全フレーム到達
（位相整合・ハードウェアとも 150/150、破損ゼロ）で、ここでは測れる到達率差は無い
——位相整合のシンボル展開は既定設定・テスト負荷では悪影響なし。（以前 hw で一度
出た 23/25 は一過性のRFばらつき。）これは合否ゲートではなく計測スタディで、各モードが
開けることと両モードで encoder が正準のままであることだけを assert するので、位相整合
の到達率低下は失敗でなく所見として出る。実行:

```sh
uv run --env-file .env pytest -s -o python_files="study_*.py" \
  studies/compat_matrix_ac/irremoteesp8266_rx/study_carrier_ab.py
```

### デコーダ許容（タイミングスキュー）

`heatpumpir_tx` はデコーダの厳しすぎる点も炙り出した。HeatpumpIR のESP32送信は
マーク毎にLEDCキャリアを付け直すbusy-loopビットバンガーで、空白が約+150us伸びる
（zero空白が公称432usに対し捕捉で約620us）。元の0/1判定は各長の狭い窓を使い、
その間のデッドゾーンでこれらのフレームを丸ごと棄却していた — HeatpumpIRが問題なく
制御できる実機Panasonicユニットより厳しい。空白分類を0と1の長さの中点しきい値
（とフレーム間ギャップ用の別の大きな終端しきい値）に変更し、整合性は checksum で
担保しつつ実機並みのスキューを許容するようにした。捕捉した HeatpumpIR フレームを
host回帰テスト（`testPanasonicAcDecodesSkewedTiming`）として固定。
