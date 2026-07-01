# protocol matrix (エアコン)

> English: [README.md](README.md)

`protocol_matrix_ac/` は `protocol_matrix/` のエアコン版です。`esp32irpk::ac` レイヤーについて、ESP32IRPulseKit の自前TX -> 自前RX を実機で確認します（外部ライブラリ非使用）。リリース前確認に含めます。

peer は各ベンダの既定 known-good 状態を `ac::send` で送信し、RX 親は `ac::decodeAny` でベンダを判定して該当ベンダの `Frame` で再デコードし、復元したバイトを返します。復元したベンダ＋状態バイトが peer の送信内容と一致し、チェックサムが有効なら PASS です。自己往復ゲートには各ベンダ1代表状態で十分で、mode/fan/温度の網羅は host の `pc/codec_smoke` が担います。

実装間の相互運用（IRremoteESP8266・HeatpumpIR）は別問題で、`studies/compat_matrix_ac/` が担当します。

親sketchをRX、`peer_tx/` をTXに固定します。peer名は `tx` のままで、ポート指定は `TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3` を使います。peer は位相整合キャリア（ライブラリ既定。Gree・Daikin など一部ベンダが必須）で送信するため、このゲートにハードウェアキャリアモードはありません。

## 対象（各ベンダ1つ）

- PANASONIC
- GREE
- MITSUBISHI
- FUJITSU
- DAIKIN
- TOSHIBA
- SAMSUNG
- SHARP
- KELVINATOR
- MIDEA

## 実行

```sh
cd tests
uv run --env-file .env pytest hardware/protocol_matrix_ac/
```

pytestは `PROTOCOL_MATRIX_AC` として、ベンダ・往復比率・状態バイトを出力します。
