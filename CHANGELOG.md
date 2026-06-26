# Changelog / 変更履歴

## Unreleased
- (EN) Panasonic A/C: quiet/powerful comfort modes are now `Fan::QUIET`/`Fan::POWERFUL` (byte21 flags, fan stays auto). **Breaking:** the old `Fan::QUIET`/`POWERFUL` (which were the slowest/fastest fan *speeds*) are renamed `Fan::MIN_SPEED`/`Fan::MAX_SPEED`; the `Fan` enumerator values also shifted.
- (JA) Panasonic エアコン: しずか/パワフルを `Fan::QUIET`/`Fan::POWERFUL`（byte21 フラグ、fan は auto のまま）として追加。**破壊的変更:** 最弱/最強の*風量*を指していた旧 `Fan::QUIET`/`POWERFUL` は `Fan::MIN_SPEED`/`Fan::MAX_SPEED` に改名（enum の整数値も変化）。
- (EN) Panasonic A/C: 0.5 °C setpoints and louver. `temperatureC()` and `setTemperatureC()` are now a symmetric `float` pair carrying 0.5 °C steps (e.g. `22.5`); `halfDegree()` is a convenience reader. `louver()`/`setLouver()` with a `Louver` enum sets the vertical swing position. Field offsets confirmed against a real remote (ACXA75C15870).
- (JA) Panasonic エアコン: 0.5℃設定とルーバーに対応。`temperatureC()` と `setTemperatureC()` を 0.5℃刻みを含む対称な `float` ペアに（例 `22.5`）。`halfDegree()` は簡便な読み取り。`Louver` enum 付きの `louver()`/`setLouver()` で垂直スイング位置を設定。フィールド位置は実機（ACXA75C15870）で確認。
- (EN) A/C model parameter: each vendor frame can carry a `Model` (format = separate `Frame` type, model = parameter; `fromRaw` auto-detects). Panasonic now supports `Model` JKE/DKE/NKE/LKE/RKR (shared field map, per-model marker bytes), and Gree carries `Model::YBOFB`. Encoding an unimplemented model returns `false`.
- (JA) エアコンのモデルパラメータ: 各ベンダの Frame が `Model` を持てるように（フォーマット＝別 `Frame` 型、モデル＝パラメータ、`fromRaw` が自動判定）。Panasonic は `Model` JKE/DKE/NKE/LKE/RKR に対応（フィールドマップ共通、モデル別マーカーバイト）、Gree は `Model::YBOFB`。未実装モデルのエンコードは `false`。
- (EN) Mitsubishi air-conditioner support (`esp32irpk::ac::Mitsubishi`): decode/encode of the 18-byte "Mitsubishi AC" state (5-byte signature, sum checksum, frame sent twice), and `Mode`/`Fan` accessors. `06_ac_learn` recognizes it too.
- (JA) Mitsubishi エアコン対応（`esp32irpk::ac::Mitsubishi`）: 18バイトの「Mitsubishi AC」状態（5バイト署名、総和チェックサム、フレームを2回送信）のデコード/エンコードと `Mode`/`Fan` アクセサを追加。`06_ac_learn` も認識します。

## 1.0.2
- (EN) Gree air-conditioner support (`esp32irpk::ac::Gree`): decode/encode of the 8-byte two-block state (second block has no header), Kelvinator block checksum, and `Mode`/`Fan` accessors. `06_ac_learn` now recognizes Gree as well as Panasonic.
- (JA) Gree エアコン対応（`esp32irpk::ac::Gree`）: 8バイト2ブロック状態（2ブロック目はヘッダ無し）のデコード/エンコード、Kelvinatorブロックチェックサム、`Mode`/`Fan` アクセサを追加。`06_ac_learn` が Panasonic に加えて Gree も認識します。

## 1.0.1
- (EN) Air-conditioner support: a separate `esp32irpk::ac` layer over the RAW path, with Panasonic decode/encode (`Frame::fromRaw`/`toRaw`, `Mode`/`Fan`, sum checksum) and a one-call `ac::send`. Verified field-for-field against IRremoteESP8266 and HeatpumpIR.
- (JA) エアコン対応: RAW経路上の独立した `esp32irpk::ac` レイヤーを追加。Panasonic のデコード/エンコード（`Frame::fromRaw`/`toRaw`、`Mode`/`Fan`、総和チェックサム）と1呼び出しの `ac::send` を提供。IRremoteESP8266 と HeatpumpIR とフィールド単位で照合済み。
- (EN) `IRReceiver::setMaxRxSymbols()` to enlarge the RX capture, enabling RAW-only learn & replay of long frames (e.g. air conditioners).
- (JA) `IRReceiver::setMaxRxSymbols()` を追加し、RXキャプチャ容量を拡張。長尺フレーム（エアコン等）のRAWのみ学習＆再送に対応。
- (EN) `IRReceiveResult` flag shortcuts: `decodeSkipped()`, `truncated()`, `rmtOverflow()`.
- (JA) `IRReceiveResult` にフラグ判定のショートカット `decodeSkipped()` / `truncated()` / `rmtOverflow()` を追加。
- (EN) New examples: `06_ac_learn` (capture an A/C remote, print a decoded summary and replay code) and `07_ac_send` (build and send Panasonic A/C frames).
- (JA) 新しい例: `06_ac_learn`（エアコンのリモコンを学習し、デコード結果と再送コードを出力）と `07_ac_send`（PanasonicのACフレームを組み立てて送信）。

## 1.0.0
- (EN) Added changelog
- (JA) チェンジログ追加
