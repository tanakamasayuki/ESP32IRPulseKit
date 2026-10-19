# GPIO Lチカ（手動）

> English: [README.md](README.md)

ライブラリを使わずにIR送信ピンを確認します。スケッチは `TEST_IR_TX_GPIO` を普通のGPIO書き込みでゆっくり切り替えます（ESP32IRPulseKit・RMT・キャリアのいずれも不使用）。IR LEDをスマホのカメラ越し（IRを通しやすいのは通常インカメラ）に見て、配線・駆動回路の問題かライブラリの問題かを切り分けます。

- LEDが点滅に追従する → ピンと駆動回路は正常。それでもIR送信が失敗するならライブラリ側を疑う。
- LEDが追従しない → ピンか駆動回路の問題。

ピンは入出力両用に設定しているため、レベルを変えるたびにパッドの実際の読み値も出力します（`GPIO set=1 read=1 led=on`）。`set` と `read` が食い違う場合、ピンが駆動したレベルに達していません。モジュールのDATA入力がピンを引っ張っているか、ピンの指定違い・入力専用ピンです（ESP32のGPIO34〜39は起動時に `GPIO_ERROR not_output_capable` で弾きます）。

## 実行

`tests/` から:

```sh
# ESP32-S3 の送信ボード（既定）
uv run python studies/gpio_blink/blink.py

# ESP32無印の送信ボード
uv run python studies/gpio_blink/blink.py --peer-profile tx:tx_esp32

# LEDを点灯したまま保持してテスターでピンを測る
uv run python studies/gpio_blink/blink.py --peer-profile tx:tx_esp32 --hold on

# パッドの駆動力を最大（GPIO_DRIVE_CAP_3）にする（モジュール入力が電流を必要とする場合）
uv run python studies/gpio_blink/blink.py --peer-profile tx:tx_esp32 --drive 3
```

オプション: `--period MS`（切り替え周期、既定1000）、`--hold on|off`、`--drive 0-3`、`--no-flash`。Ctrl-Cで停止し、読み戻し不一致の回数を含むまとめを表示します。

ポートは `tests/.env` の `TEST_SERIAL_PORT_PEER_TX_<PROFILE>`（2台テストで送信ボードに使うのと同じキー）、ピンは `TEST_IR_TX_GPIO` / `TEST_IR_TX_INVERTED` から取得します。`IR_TX_INVERTED=1` のときは「LED点灯」でピンをLowにします。

## シリアルコマンド

| コマンド | 応答 |
| --- | --- |
| `READY` | `GPIO_READY gpio=<n> inverted=<0|1> output_capable=<0|1> drive=<0-3>` |
| `PING` | `PONG` |
| `ON` / `OFF` | `GPIO set=<0|1> read=<0|1> led=<on|off>`（レベルを保持） |
| `BLINK <ms>` | `GPIO_BLINK <ms>`、以降は切り替えごとに `GPIO set=...` |
| `DRIVE <0-3>` | `GPIO_DRIVE <n>` |
