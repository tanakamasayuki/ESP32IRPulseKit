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
panasonic_irremoteesp8266_tx/        # TX: IRremoteESP8266（既知状態） -> RX: ESP32IRPulseKit  （当方decode較正）
panasonic_irremoteesp8266_rx/        # TX: ESP32IRPulseKit -> RX: IRremoteESP8266              （当方encode検証）
panasonic_irremoteesp8266_self/      # TX + RX: IRremoteESP8266                               （参照ベースライン）
panasonic_heatpumpir_tx/             # TX: HeatpumpIR（既知状態） -> RX: ESP32IRPulseKit       （2系統目の参照）

# Gree（esp32irpk::ac::Gree に合わせて IRGreeAC の YBOFB モデルを使用）
gree_irremoteesp8266_tx/   # TX: IRremoteESP8266（既知状態） -> RX: ESP32IRPulseKit  （当方decode較正）
gree_irremoteesp8266_rx/   # TX: ESP32IRPulseKit -> RX: IRremoteESP8266              （当方encode検証）

# Fujitsu（esp32irpk::ac::Fujitsu に合わせて IRFujitsuAC の ARRAH2E モデルを使用）
fujitsu_irremoteesp8266_tx/  # TX: IRremoteESP8266（既知状態） -> RX: ESP32IRPulseKit  （当方decode較正）
fujitsu_irremoteesp8266_rx/  # TX: ESP32IRPulseKit -> RX: IRremoteESP8266              （当方encode検証）

# Daikin（esp32irpk::ac::Daikin に合わせて IRDaikinESP のクラシック ARC433 を使用）
daikin_irremoteesp8266_tx/   # TX: IRremoteESP8266（既知状態） -> RX: ESP32IRPulseKit  （当方decode較正）
daikin_irremoteesp8266_rx/   # TX: ESP32IRPulseKit -> RX: IRremoteESP8266              （当方encode検証）

# Toshiba（esp32irpk::ac::Toshiba に合わせて IRToshibaAC の標準9バイト TOSHIBA_AC を使用）
toshiba_irremoteesp8266_tx/  # TX: IRremoteESP8266（既知状態） -> RX: ESP32IRPulseKit  （当方decode較正）
toshiba_irremoteesp8266_rx/  # TX: ESP32IRPulseKit -> RX: IRremoteESP8266              （当方encode検証）

# Samsung（esp32irpk::ac::Samsung に合わせて IRSamsungAc の標準14バイト SAMSUNG_AC を使用）
samsung_irremoteesp8266_tx/  # TX: IRremoteESP8266（既知状態） -> RX: ESP32IRPulseKit  （当方decode較正）
samsung_irremoteesp8266_rx/  # TX: ESP32IRPulseKit -> RX: IRremoteESP8266              （当方encode検証）

# Sharp（esp32irpk::ac::Sharp に合わせて IRSharpAc の標準13バイト SHARP_AC、A907モデルを使用）
sharp_irremoteesp8266_tx/    # TX: IRremoteESP8266（既知状態） -> RX: ESP32IRPulseKit  （当方decode較正）
sharp_irremoteesp8266_rx/    # TX: ESP32IRPulseKit -> RX: IRremoteESP8266              （当方encode検証）

# Kelvinator（esp32irpk::ac::Kelvinator に合わせて IRKelvinatorAC の標準16バイト2ブロックを使用）
kelvinator_irremoteesp8266_tx/  # TX: IRremoteESP8266（既知状態） -> RX: ESP32IRPulseKit  （当方decode較正）
kelvinator_irremoteesp8266_rx/  # TX: ESP32IRPulseKit -> RX: IRremoteESP8266              （当方encode検証）

# Midea（esp32irpk::ac::Midea に合わせて IRMideaAC の標準48ビット/6バイト二重送信を使用）
midea_irremoteesp8266_tx/    # TX: IRremoteESP8266（既知状態） -> RX: ESP32IRPulseKit  （当方decode較正）
midea_irremoteesp8266_rx/    # TX: ESP32IRPulseKit -> RX: IRremoteESP8266              （当方encode検証）

# Carrier（esp32irpk::ac::Carrier に合わせて IRCarrierAc64 の CARRIER_AC64 8バイト/64ビットを使用）
carrier_irremoteesp8266_tx/  # TX: IRremoteESP8266（既知状態） -> RX: ESP32IRPulseKit  （当方decode較正）
carrier_irremoteesp8266_rx/  # TX: ESP32IRPulseKit -> RX: IRremoteESP8266              （当方encode検証）

# Hitachi（esp32irpk::ac::Hitachi に合わせて IRHitachiAc の HITACHI_AC 28バイトを使用）
hitachi_irremoteesp8266_tx/  # TX: IRremoteESP8266（既知状態） -> RX: ESP32IRPulseKit  （当方decode較正）
hitachi_irremoteesp8266_rx/  # TX: ESP32IRPulseKit -> RX: IRremoteESP8266              （当方encode検証）

# Haier（esp32irpk::ac::Haier に合わせて IRHaierAC の HAIER_AC 9バイトを使用）
haier_irremoteesp8266_tx/    # TX: IRremoteESP8266（既知状態） -> RX: ESP32IRPulseKit  （当方decode較正）
haier_irremoteesp8266_rx/    # TX: ESP32IRPulseKit -> RX: IRremoteESP8266              （当方encode検証）
```

各ベンダは同じ `<extlib>_<role>` バリアントを使う。バリアントのフォルダ名は
すべてベンダ名で前置する（`panasonic_…`、`gree_…`）。

`panasonic_irremoteesp8266_self`（IRremoteESP8266 が既知状態をエンコードし、自分の送信を
デコード）はベースライン。クロス方向を信頼する前に、参照が物理治具/配置で往復
できることを確認します。

Arduino-IRremote はここでは**使いません**。エアコン状態のデコーダを持たず
（Panasonic を汎用48bitフレームとしてしか読まず、AC状態は復号しない）、フィールド
比較ができないためです。

- `*_tx`（外部が送信、当方RX）: peer が**既知**のAC状態（例: 冷房/26℃/風量自動）を
  送信。当方RXがRAWをキャプチャ（RAWのみ＋`setMaxRxSymbols`＋大きめidle）し
  `ac::Panasonic::Frame::fromRaw` でデコードしてフィールドを出力。study は
  `送信状態 → 当方デコード結果` を記録＝これがフィールドマップ確定の材料。
- `panasonic_irremoteesp8266_rx`（当方が送信、外部RX）: 当方 `ac::Panasonic` で状態を
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
  studies/compat_matrix_ac/panasonic_irremoteesp8266_tx/
```

## 状態

設計とシリアル契約はここで確定。バリアント別のsketchとハーネスは1つずつ追加。
実装済み:

- `panasonic_irremoteesp8266_self/` — ベースライン: 物理治具がPanasonic ACフレームを往復
  できること（IRremoteESP8266 がエンコード→送信→受信→同じ27バイトへデコード）を、
  クロス方向を信頼する前に確認。
- `panasonic_irremoteesp8266_tx/` — フィールドマップ較正: IRremoteESP8266 が既知状態を送信、
  当方RXがRAWキャプチャして `esp32irpk::ac::Panasonic` でデコード。正準フレームとの
  バイト一致が合否判定（hard）、既知状態とのフィールド単位比較は報告のみ（assertし
  ない）でフィールドマップ確定の材料にする。`test_irremoteesp8266_tx_models` は加えて、
  peer に各Panasonicモデル（`setModel` DKE/NKE/LKE/RKR）を送らせ、当方RXが正準バイトを
  復元し**かつ同じモデルを自己判定**（`Frame::model`）することを確認 — モデル別マーカー
  バイトと判定を IRremoteESP8266 に対して検証する。
- `panasonic_irremoteesp8266_rx/` — encoder検証（`_tx`の対）: 当方TXが `esp32irpk::ac::Panasonic`
  で既知状態をエンコードして送信、IRremoteESP8266 がデコード。外部ライブラリが復元
  したバイトが当方encoderの生成バイトと一致（checksum ok）すること＝当方 `toRaw()` が
  独立スタックも受理する正しいバーストを出すことの証明。

- `panasonic_heatpumpir_tx/` — 2系統目の独立参照: HeatpumpIR（`PanasonicJKEHeatpumpIR`、
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
  位相整合 50/50 vs ハードウェア 約55%。`study_gree_carrier_ab.py`）。位相整合でも540usの
  zeroスペースはスペースを伸ばす受光に対し余裕が小さいので、参照デコーダが既定許容で
  受理するにはIR経路がそれなりに整列している必要がある。

- `mitsubishi_irremoteesp8266_tx/` + `mitsubishi_irremoteesp8266_rx/` — 18バイトの
  「Mitsubishi AC」protocol 用の較正＋encoder検証の対。IRremoteESP8266 の
  `IRMitsubishiAC` を使う。フレームは単一のpulse-distance（5バイト署名、最終バイトに
  総和チェックサム）を約15.5msギャップで2回送る構成で、`rx` primary は両コピーを1
  キャプチャに収めるため50msタイムアウトを使う。Gree同様、zeroスペース（420us）が
  bitマーク（450us）より短いので位相整合キャリアで送る（`rx` study が設定。
  `study_mitsubishi_carrier_ab.py` で位相整合 vs ハードウェアを計測）。

- `fujitsu_irremoteesp8266_tx/` + `fujitsu_irremoteesp8266_rx/` — 「Fujitsu AC」
  protocol（モデル ARRAH2E）用の較正＋encoder検証の対。IRremoteESP8266 の
  `IRFujitsuAC` を使う。全設定は16バイトの長フレーム（固定 `14 63 00 10 10`、byte5
  = `0xFE`、byte15 に補数チェックサム）、電源OFFは7バイトの短フレーム
  （`14 63 00 10 10 02 FD`）なので、ケースには短フレームを通す `power=0` を含める。
  フレームは1回送信で、`rx` primary は50msタイムアウトを使う。Gree/Mitsubishi同様、
  zeroスペース（390us）がbitマーク（448us）より短いので位相整合キャリアで送る。当方
  `esp32irpk::ac::Fujitsu` のenum値は IRremoteESP8266 のワイヤコードと一致するので、
  フィールド比較は直接の数値一致になる。

- `daikin_irremoteesp8266_tx/` + `daikin_irremoteesp8266_rx/` — クラシックな「Daikin」/
  ARC433 protocol 用の較正＋encoder検証の対。IRremoteESP8266 の `IRDaikinESP` を使う。
  35バイト状態を5bitの `00000` プリアンブル＋3セクション（8 / 8 / 19バイト）として送り、
  各セクションは独自の `3650/1623µs` ヘッダとセクション毎の総和チェックサム（byte7 / 15 /
  34）を持ち、各セクションは `11 DA 27` 署名で始まる。`tx` primary はプリアンブルを読み飛ばし
  3セクションを復号、`rx` peer は `toRaw` でプリアンブル＋セクションを描画する。`rx` primary は
  ~29msのセクション間ギャップを1キャプチャに収めるため65msタイムアウトを使う。Daikin は最も
  タイミング余裕が狭い（zeroスペース428us == bitマーク428us）ので位相整合キャリアで送る。当方
  `esp32irpk::ac::Daikin` のenum値は IRremoteESP8266 のワイヤコードと一致するので、フィールド比較は
  直接の数値一致になる。

- `toshiba_irremoteesp8266_tx/` + `toshiba_irremoteesp8266_rx/` — 標準9バイト TOSHIBA_AC
  用の較正＋encoder検証の対。IRremoteESP8266 の `IRToshibaAC` を使う。`F2 0D` 署名＋byte8 XOR
  チェックサムの単一 **MSB-first** フレーム（唯一のMSB-first ACベンダ）。電源は Mode フィールドの
  off コード（7）。codec の MSB-first 経路を実機で検証する。

- `samsung_irremoteesp8266_tx/` + `samsung_irremoteesp8266_rx/` — 標準14バイト SAMSUNG_AC
  用の較正＋encoder検証の対。IRremoteESP8266 の `IRSamsungAc` を使う。LSBファースト、一回限りの
  先頭ヘッダ（690/17844us）の後に7バイト×2セクション、各セクションは popcount チェックサムを
  バイト1〜2に分割格納。電源は2bitフィールド2つ。TX peer は `IRsend::sendSamsungAC` を直接呼ぶ
  （`IRSamsungAc::send` は電源変化時に21バイト拡張フレームを出すため）。RX は17.8msの先頭ギャップを
  跨ぐため30msタイムアウト。`samsung_heatpumpir_tx` は**無い**: HeatpumpIR の Samsung クラスは
  旧 AQV（21バイト）/ FJM（section2チェックサムが別）変種で現行14バイト形式と一致しないため、
  Samsung は IRremoteESP8266 双方向ペアのみで相互検証する。

- `sharp_irremoteesp8266_tx/` + `sharp_irremoteesp8266_rx/` — 標準13バイト SHARP_AC（既定
  A907モデル）用の較正＋encoder検証の対。IRremoteESP8266 の `IRSharpAc` を使う。LSBファースト、
  単一フレーム、固定ヘッダ `AA 5A CF 10`、byte12上位ニブルにニブル畳み込みXORチェックサム。電源は
  4bitの `PowerSpecial`（on=3/off=2）。`Special` バイトは押されたボタンを表すため、TX peer は
  `setPower` を**最後**に呼ぶ（Special=power=0x00 となり当方encoderの出力と一致）。Auto/Dry は
  温度を持たないので temp はその場では don't-care。Samsung 同様 `sharp_heatpumpir_tx` は**無い**
  （HeatpumpIR に Sharp は無い）ので、IRremoteESP8266 双方向ペアのみで相互検証する。

- `gree_heatpumpir_tx/` + `mitsubishi_heatpumpir_tx/` + `fujitsu_heatpumpir_tx/` + `daikin_heatpumpir_tx/` + `toshiba_heatpumpir_tx/` — それぞれの2系統目の独立参照
  （`panasonic_heatpumpir_tx` と同型）。HeatpumpIR（`GreeGenericHeatpumpIR` /
  `MitsubishiFEHeatpumpIR` / `FujitsuHeatpumpIR`、別コードベース、LEDCキャリア）が
  既知状態を送信し当方RXがデコード。hard判定は意味一致（checksum妥当＋論理フィールドが
  送信状態と一致）で、バイト一致ではない（HeatpumpIRはIRremoteESP8266と異なる補助バイトを
  埋めるため）。HeatpumpIR の Mitsubishi fan は QUIET/HIGH に届き、fan-auto を専用
  FanAutoビット無しで符号化するので、それらのデコード経路も検証できる。HeatpumpIR の
  Fujitsu は同じ ARRAH2E 長フレーム（byte5=0xFE）を、当方と同値に還元されるチェックサムで
  出力し、fan定数がワイヤコードと逆順（FAN_1=quiet .. FAN_4=high）なので、peer は各速度
  トークンを目的のワイヤコードを生む FAN_x にマップする。HeatpumpIR の Daikin は同じクラシック
  35バイト・3セクションの ARC433 フレームを出力し、テンプレートのセクション1/2チェックサムを保ち
  byte34 のみ再計算するので当方の3チェックサム検証が通る。Daikin encoder はスイングを駆動せず
  （固定off）quiet段も無いので、この variant は power / mode / temp / fan を較正する。
  HeatpumpIR の Toshiba は状態をビット反転して送るので、ワイヤ上は当方が直接読める標準の
  MSB-first TOSHIBA_AC フレームになる。FAN 運転モードは無く、スイングも駆動しないので、この
  variant は power / mode（auto/cool/dry/heat）/ temp / fan を較正する。

所見と確定したフィールドマップはここと `src/ac/Panasonic.h` / `src/ac/Gree.h` /
`src/ac/Mitsubishi.h` / `src/ac/Fujitsu.h` / `src/ac/Daikin.h` / `src/ac/Toshiba.h` / `src/ac/Samsung.h` / `src/ac/Sharp.h` に反映します。

`panasonic_irremoteesp8266_tx/` の結果、`src/ac/Panasonic.h` のPanasonicフィールドマップが
IRremoteESP8266 の `IRPanasonicAc` とバイト単位で一致することを確認: 当方のRAW
キャプチャが正準27バイトを再現し、power / mode（auto/cool/heat/dry）/ temperature
/ fan がすべて期待値にデコードされる。fan nibble は Panasonic の速度＋3
（min/low/med/high/max = 0x3〜0x7、auto = 0xA）。

`panasonic_irremoteesp8266_rx/` の結果、当方encoderも確認: setterで構築し `toRaw()` で描画した
フレームが、実機リモコンが常に持つ固定feature byte（[15]=0x80, [19]=0x0E, [20]=0xE0,
[23]=0x81）を含む完全な正準状態になる。そのため `Frame` は既知良テンプレートを既定値
とし、mode/temp/fan/power だけ設定してもこれらのバイトが揃う。

### キャリア信頼性（長尺TX）

ACのサンプルは `setPhaseAlignedCarrier(false)` で自走ハードウェアキャリアを選ぶ
（無指定ならライブラリのキャリアは位相整合）。位相整合はマーク精度が高くなり得るが、
長尺ACバーストを1送信あたり約17KBのシンボルに展開し、重い割り込み負荷下では
リフィルのアンダーランリスクが増す（SPEC §11.3）。そのためサンプルは長尺フレームで
ハードウェアキャリアを推奨する。

`study_panasonic_irremoteesp8266_carrier_ab.py` はその選択が到達率を損なうかを計測する。`panasonic_irremoteesp8266_rx`
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
  studies/compat_matrix_ac/panasonic_irremoteesp8266_rx/study_panasonic_irremoteesp8266_carrier_ab.py
```

### デコーダ許容（タイミングスキュー）

`panasonic_heatpumpir_tx` はデコーダの厳しすぎる点も炙り出した。HeatpumpIR のESP32送信は
マーク毎にLEDCキャリアを付け直すbusy-loopビットバンガーで、空白が約+150us伸びる
（zero空白が公称432usに対し捕捉で約620us）。元の0/1判定は各長の狭い窓を使い、
その間のデッドゾーンでこれらのフレームを丸ごと棄却していた — HeatpumpIRが問題なく
制御できる実機Panasonicユニットより厳しい。空白分類を0と1の長さの中点しきい値
（とフレーム間ギャップ用の別の大きな終端しきい値）に変更し、整合性は checksum で
担保しつつ実機並みのスキューを許容するようにした。捕捉した HeatpumpIR フレームを
host回帰テスト（`testPanasonicAcDecodesSkewedTiming`）として固定。
