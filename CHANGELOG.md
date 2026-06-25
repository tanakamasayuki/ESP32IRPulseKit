# Changelog / 変更履歴

## Unreleased

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
