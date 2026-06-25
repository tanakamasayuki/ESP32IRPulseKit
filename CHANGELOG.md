# Changelog / 変更履歴

## Unreleased
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
