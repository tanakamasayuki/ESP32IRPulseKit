# ESP32IRPulseKit

> English: [README.md](README.md)

ESP32 Arduino Core 3.x 向けのIRリモコン送受信ライブラリです。ESP-IDF 5.x の新RMTドライバ上で動作します。

RAWなmark/space波形をキャプチャし、スコア付きのプロトコル候補とともに正規化bitsへデコードします。送信は位相整合キャリアを用い、他ライブラリとの相互運用でも安定して復調できます。

## 特長

- RMTベースのTX/RX。エンベロープはハードウェアタイミング、受信はハードウェアタイムスタンプで、エッジごとの割り込みジッタがありません。
- 3つの抽象レベルで扱えます。RAW tick（`1 tick = 10us`）、正規化された `IRDecodedBits`、プロトコル固有の `Frame` 型。
- 複数候補デコード。各キャプチャを登録プロトコルに対してスコア付けし、上位順で返すため、似たプロトコルも区別できます。
- 既定で位相整合・シンボルエンコードのキャリアを使用。復調後のmarkがフレーム間で安定し、他ライブラリでもきれいにデコードされます。
- 専用デコーダのないプロトコルを含め、あらゆる波形をRAW経路で学習・再送できます。

## 対応プロトコル

| Protocol ID | ビット数 | 備考 |
|---|---|---|
| `NEC` | 32 | アドレス＋コマンド＋リピートフレーム |
| `AEHA` | 可変 | 家製協（Kaseikyo/Panasonic）系。ファミリ全体で1つのID |
| `SONY12` / `SONY15` / `SONY20` | 12 / 15 / 20 | SIRC, 40kHz |
| `SAMSUNG32` / `SAMSUNG36` | 32 / 36 | |
| `JVC` | 16 | |
| `RC5` | 14 | バイフェーズ, 36kHz |
| `RC6_M0_16` / `RC6_M6_32` | 21 / 36 | バイフェーズ, 36kHz |

専用デコーダがない波形でも、RAW tick経路でキャプチャと再送が可能です。

## インストール

- Arduino IDE: ライブラリマネージャで `ESP32IRPulseKit` を検索。
- 手動: 本リポジトリを Arduino の `libraries/` ディレクトリへコピー。
- ESP32 Arduino Core 3.0 以降（ESP-IDF 5.x の新RMTドライバ）が必要です。

ヘッダは1つだけインクルードします。

```cpp
#include <ESP32IRPulseKit.h>
```

## クイックスタート

受信してデコード:

```cpp
#include <ESP32IRPulseKit.h>

esp32irpk::IRReceiver rx(32, true); // GPIO 32。多くの受信モジュールは出力が反転

void setup() {
  Serial.begin(115200);
  rx.begin();
}

void loop() {
  esp32irpk::IRReceiveResult<> r;
  if (!rx.read(r)) {
    delay(1);
    return;
  }
  if (const esp32irpk::IRDecodedBits *bits = r.bits()) {
    Serial.print("protocol=");
    Serial.print((unsigned)bits->protocol_id);
    Serial.print(" bits=0x");
    Serial.println((uint32_t)bits->bits, HEX);
  }
}
```

NEC送信:

```cpp
#include <ESP32IRPulseKit.h>

esp32irpk::IRSender tx(4); // GPIO 4

void setup() {
  Serial.begin(115200);
  tx.begin();
}

void loop() {
  tx.send(esp32irpk::bits::nec(0x00ff, 0x34));
  delay(1000);
}
```

送信側は `begin()` で全内蔵プロトコルを登録するため、`send()` は各プロトコルの推奨キャリアを自動的に使います。

## 設定（初期化パラメータ）

### 受信側（`begin()` より前に呼ぶ）

| メソッド | 既定値 | 備考 |
|---|---|---|
| `IRReceiver<MaxCandidates>(gpio, inverted)` | `MaxCandidates = 4` | テンプレート引数が保持できる候補数の上限 |
| `setDecodeCandidates(n)` | `MaxCandidates` | `0..MaxCandidates`。`0` でRAWのみモード（デコードなし） |
| `setIdleThresholdUs(us)` | `30000` | RMTの無信号しきい値。本値と登録protocolの値の大きい方を使用 |
| `setScoreThreshold(score)` | `0` | このスコア未満の候補は破棄 |
| `addProtocol(spec)` / `clearProtocols()` | 全内蔵 | specを登録してデコード対象を限定・上書き |

### 送信側

| メソッド | 既定値 | タイミング | 備考 |
|---|---|---|---|
| `setCarrierHz(hz)` | `38000` | begin前後 | 明示的な上書き。範囲 `20000..60000`。protocol推奨より優先 |
| `clearCarrierHz()` | — | begin前後 | 上書きを解除し、protocol/ライブラリ既定に戻す |
| `disableCarrier()` | キャリアあり | begin前後 | キャリア変調なしのソリッドmarkで送信 |
| `setCarrierDuty(duty)` | `0.33` | begin前後 | キャリアのオン時間比率。`0 < duty < 1` |
| `setPhaseAlignedCarrier(enable)` | `true` | begin前のみ | キャリア生成方式 — 下記参照 |
| `setTxMemBlocks(blocks)` | `1` ブロック | begin前のみ | RMT TXメモリブロック数。`0` でライブラリ既定 |

キャリア周波数系のメソッドは送信中は拒否されます。`setPhaseAlignedCarrier()` と `setTxMemBlocks()` はTXチャネルの構成を固定するため、`begin()` より前でのみ有効です。

### キャリアはどちらが生成するか

- **位相整合・シンボルエンコード（既定）。** RMTエンコーダが各markを位相0から始まる整数個のキャリアサイクルとして出力します。各markが決定的なサイクル数を持つため、復調後のmark（とその後のspace）がフレーム間で安定し、他ライブラリでのデコードに最も適します。代償はフレームあたりのRMTシンボル数の増加（おおよそキャリア1サイクルにつき1シンボル）で、ISR負荷が高い用途（例: フラッシュ書き込みと並行）では `setTxMemBlocks()` でストリーミングの余裕を増やせます。
- **フリーランのハードウェアキャリア**（`setPhaseAlignedCarrier(false)`）。RMTペリフェラルが自前のキャリアを重畳します（`rmt_apply_carrier`）。シンボル数は大幅に少ない一方、markごとに位相がリセットされないため、復調後のmarkが±1キャリアサイクル（38kHzで約26µs）揺れます。これによりmarkの短いprotocol（JVC・AEHA）が、外部デコーダの厳しい判定窓を外れることがあります。

キャリアとタイミングモデルの全体は [DESIGN.ja.md](DESIGN.ja.md) §8・§12 を参照してください。

## サンプル

| サンプル | 説明 |
|---|---|
| [01_rx_dump](examples/01_rx_dump) | 受信・デコードし、候補とフレームをシリアル出力 |
| [02_nec_tx](examples/02_nec_tx) | NECフレームの送信 |
| [03_send_protocols](examples/03_send_protocols) | 内蔵プロトコルを1フレームずつ送信 |
| [04_learn](examples/04_learn) | リモコン学習。受信し、再送用のC++コードを貼り付け可能な形で出力 |
| [05_raw_monitor](examples/05_raw_monitor) | RAWのみのキャプチャと受信統計 |

## ドキュメント

- [SPEC.ja.md](SPEC.ja.md) — 公開APIの仕様
- [DESIGN.ja.md](DESIGN.ja.md) — 実装メモ、スコアリングとキャリアモデル
- [tests/TEST_PLAN.ja.md](tests/TEST_PLAN.ja.md) — テスト方針

## テスト

テストは `tests/` に集約します。

```sh
cd tests
cp .env.example .env
# Edit .env for your local serial ports and GPIOs.
uv run --env-file .env pytest pc
uv run --env-file .env pytest hardware/link_smoke
```

`pytest pc` はPCテスト一式（`fixtures`、`codec_smoke`、`compile`）を実行します。`hardware/` は2台構成の合否回帰テスト、`studies/` 配下の実機調査は自動収集されません（`study_*.py`）。詳細は [tests/README.ja.md](tests/README.ja.md) を参照してください。

## ライセンス

MITライセンス。[LICENSE](LICENSE) を参照してください。
