# RMT共存 1ボードテスト

> English: [README.md](README.md)

ライブラリのRMTチャンネルと他のRMT利用者が、1つのチップ上でどの順番で作っても共存できることを確認します。

- `IRReceiver`、`IRSender`（位相同期キャリアと変調）、Arduino RMT API（`rmtInit()`）によるWS2812相当の10MHzチャンネルが、試すどの順番でも `begin()` できる。
- その後、各 `IRSender` が短いフレームを送信できる。

RMTのクロック源はチップ上の全チャンネルで共有され、ESP32 / ESP32-S2 以外のSoCでは分周器も共有されて最初に作ったチャンネルが決めます。クロック源が違う、または分解能が離れすぎたチャンネルは `begin()` に失敗します（`group clock conflict`、`channel prescale out of the range`）。

RXボード1台（`dut`、peerなし）で動き、IRの配線は不要です。TXチャンネルは空きピン（ESP32はGPIO 25/26、それ以外は5/6）に出力するので、何もつながっていなければ構いません。

## シリアルプロトコル

| コマンド | 応答 |
| --- | --- |
| `READY` | `COEXIST_READY` |
| `RUN` | ケースごとに `CASE <name> <OK\|FAIL> <step>=<0\|1> ...` を1行、最後に `DONE` |

```sh
cd tests
uv run --env-file .env pytest hardware/rmt_coexist --profiles all
```
