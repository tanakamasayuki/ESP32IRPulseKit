# Changelog / 変更履歴

## Unreleased
- (EN) New optional header `<IRDebug.h>` (`esp32irpk::debug`): Serial-formatting helpers shared by the learn/dump sketches — `printRawMicros`, `printDecodedCandidate`, `printDecodedFrame` (per-protocol named fields), `printBitsSendSnippet`, `printRawSendSnippet`, and `printHexU64`/`printHexU64Literal`. Examples 01/04/05/06 and the dump study now use these instead of each re-implementing them (the duplicated ~140-line protocol decode switch is now one helper). Opt-in (`#include <IRDebug.h>`), diagnostics only — not part of the send/receive contract.
- (JA) 新しい任意ヘッダ `<IRDebug.h>`（`esp32irpk::debug`）: 学習/ダンプ系スケッチが共有するシリアル整形ヘルパー — `printRawMicros`・`printDecodedCandidate`・`printDecodedFrame`（protocol別の名前付きフィールド）・`printBitsSendSnippet`・`printRawSendSnippet`・`printHexU64`/`printHexU64Literal`。例 01/04/05/06 とダンプ study はこれを使い、各自の再実装をやめた（重複していた ~140行の protocol デコード switch が1つのヘルパーに）。opt-in（`#include <IRDebug.h>`）の診断専用で、送受信契約の一部ではない。
- (EN) A/C: every vendor `Frame` now has `printTo(Print& out)` — a diagnostic dump of the common `power/mode/temp/fan/checksum` line, the vendor's own fields (louver / swing / vane), and the full state in hex, to any Arduino `Print` (e.g. `Serial`). `06_ac_learn` and the dump study use it instead of their own per-sketch print helpers.
- (JA) エアコン: 各ベンダの `Frame` に `printTo(Print& out)` を追加 — 共通の `power/mode/temp/fan/checksum` 行・ベンダ固有フィールド（louver / swing / vane）・状態全体のhexを任意の Arduino `Print`（例 `Serial`）へダンプする診断用。`06_ac_learn` とダンプ study はスケッチ独自の print ヘルパーをやめてこれを使用。
- (EN) Mitsubishi A/C: vane (vertical swing, `Vane` enum `P1`..`P5`/`AUTO`/`SWING`), wide vane (horizontal, `WideVane`), and 0.5 °C setpoints. `temperatureC()`/`setTemperatureC()` are now a symmetric `float` pair carrying 0.5 °C steps (byte 7 bit 4), with `halfDegree()` as a convenience reader. **Breaking:** `setTemperatureC` takes `float` and `temperatureC()` returns `float` (was `uint8_t`). `setVane` asserts the vane-valid bit; `setMode` resets the wide vane to MIDDLE, so set the mode first.
- (JA) Mitsubishi エアコン: ベーン（上下スイング、`Vane` enum `P1`..`P5`/`AUTO`/`SWING`）・ワイドベーン（左右、`WideVane`）・0.5℃設定に対応。`temperatureC()`/`setTemperatureC()` を 0.5℃刻みを含む対称な `float` ペアに（byte7 bit4）、`halfDegree()` は簡便な読み取り。**破壊的変更:** `setTemperatureC` は `float` 引数、`temperatureC()` は `float` を返す（旧 `uint8_t`）。`setVane` はベーン有効ビットを立てる。`setMode` はワイドベーンを MIDDLE にリセットするので先に mode を設定する。
- (EN) Gree A/C: vertical swing (`SwingV` / `setSwingV`) and horizontal swing (`SwingH` / `setSwingH`). `setSwingV` keeps the byte-0 SwingAuto bit consistent with the chosen value, so an auto-mode/position mismatch cannot be expressed.
- (JA) Gree エアコン: 上下スイング（`SwingV`/`setSwingV`）と左右スイング（`SwingH`/`setSwingH`）に対応。`setSwingV` は選んだ値に応じて byte0 の SwingAuto ビットを整合させるため、auto モードと位置の食い違いは表現できない。
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
