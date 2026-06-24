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
irremoteesp8266_tx/   # TX: IRremoteESP8266（既知状態） -> RX: ESP32IRPulseKit  （当方decode較正）
irremoteesp8266_rx/   # TX: ESP32IRPulseKit -> RX: IRremoteESP8266              （当方encode検証）
heatpumpir_tx/        # TX: HeatpumpIR（既知状態） -> RX: ESP32IRPulseKit       （2系統目の参照）
```

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

scaffold段階。設計とシリアル契約はここで確定。バリアント別のsketchとハーネスは
1つずつ追加し、まず `irremoteesp8266_tx/`（フィールドマップ較正方向）から着手。
所見と確定したフィールドマップはここと `src/ac/Panasonic.h` に反映します。
