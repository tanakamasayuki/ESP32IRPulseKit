# RX容量 2台テスト

> English: [README.md](README.md)

`setMaxRxSymbols()` で設定するRXキャプチャ容量を実機で確認します。

- `cap` シンボル以下のフレームは欠けずに届き、truncatedにならない。
- それより長いフレームは `cap` シンボルで切り詰められ、`RAW_TRUNCATED` が立つ。

peerはNパルスの合成フレーム（エアコン風のタイミング: mark 450us、space 450us/1300us 交互）を送ります。receiverはこれをN個のRMTシンボルとして受信し、欠けなければ `2N-1` tick、切り詰められれば `2*cap` tick になります。

配線・`.env`・pytest-embeddedのpeer構成は [`link_smoke/`](../link_smoke/README.ja.md) と同じです。

## シリアルプロトコル

| ボード | コマンド | 応答 |
| --- | --- | --- |
| 両方 | `PING` | `PONG` |
| 両方 | `READY` | `RX_READY ...` / `TX_READY ...` |
| RX | `CAP <symbols>` | `RX_CAP <symbols>`（receiverを開き直す。`0` = ライブラリ既定の256） |
| TX | `SEND PULSES <n>` | `TX_OK PULSES <n>` |

RXはキャプチャごとに `RX_RAW len=<ticks> truncated=<0|1>` を出力します（RAWのみモード）。

## SoCカバレッジ

標準構成はESP32-S3 2台で、キャプチャはRMT RX ping-pongでストリーム受信されます。ESP32 / ESP32-S2 はping-pongがないため、フレームがRXチャンネルのRMTハードウェアメモリに収まる必要があります。ESP32無印をRXにしてこのテストを実行すると、その経路を確認できます（そこでは `cap` を大きくしても256シンボルを超えるフレームはtruncateされる想定です）。

```sh
cd tests
uv run --env-file .env pytest hardware/rx_capacity
```
