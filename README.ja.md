# ESP32IRPulseKit

> English: [README.md](README.md)

ESP32 Arduino Core 3.x 向けのIRリモコン送受信ライブラリです。ESP-IDF 5.x の新RMTドライバ上で動作します。

RAWなmark/space波形をキャプチャし、スコア付きのプロトコル候補とともに正規化bitsへデコードします。送信は位相整合キャリアを用い、他ライブラリとの相互運用でも安定して復調できます。

## 特長

- **ビジーループ不要。** TX/RXはESP32のRMTペリフェラルで動作し、ソフトのポーリングループでビットを叩きません。受信はハードウェアタイムスタンプ、送信はノンブロッキングなので、CPUはスケッチの他の処理に使え、割り込み負荷でタイミングがずれません。
- **完全一致ではなくスコアリング方式のデコード。** 実際のIRタイミングは受信モジュール・距離・角度・キャリアduty・外乱光で系統的にずれます。固定窓を外れたら即棄却するのではなく、各プロトコルの仕様からの**逸脱量でスコア付け**するため、基準を外れた汚い波形でもビットが一意に決まる限りデコードできます。
- **似たプロトコルも判別。** 逸脱を捨てずスコアに反映するため、同じ波形に対する類似プロトコルが順位付き候補として残ります。単一のYes/No判定ではなく、最良候補＋次点とそのスコア差が得られます。
- **3つの抽象レベル。** RAW tick（`1 tick = 10us`）、正規化された `IRDecodedBits`、プロトコル固有の `Frame` 型。必要に応じて低レベルにも高レベルにも扱えます。
- **既定で位相整合・シンボルエンコードのキャリア。** 復調後のmarkがフレーム間で安定し、他ライブラリでもきれいにデコードされます。
- **あらゆる波形を学習・再送。** 専用デコーダのないプロトコルを含め、RAW経路で学習・再送できます。
- **エアコン対応。** RAW経路上の独立した `esp32irpk::ac` レイヤーとして、1ボタンで送る多バイトの状態フレームを名前付きフィールドへデコード/エンコードします（9ベンダ: Panasonic・Gree・Mitsubishi・Fujitsu・Daikin・Toshiba・Samsung・Sharp・Kelvinator）。

## 対象範囲

本ライブラリは短い民生リモコンのフレーム（NEC・Sony・AEHA など）を対象とし、意味のあるビットへのデコード、類似プロトコルの判別、再送を行います。エアコン／ヒートポンプ系のリモコン（1ボタンで多バイトの状態フレーム全体を送るもの）は汎用コーデックには乗らないため、RAW経路上の**独立した `esp32irpk::ac` レイヤー**で扱います（9ベンダに対応 — Panasonic・Gree・Mitsubishi・Fujitsu・Daikin・Toshiba・Samsung・Sharp・Kelvinator。ベンダは追加可能）。あらゆるマイナープロトコルの網羅は目指さず、対応プロトコル数の網羅性は目標ではありません。学習・再送だけでよければRAWキャプチャ/再送が任意の波形を扱えます。

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

**キャリアduty。** 実用範囲はおおよそ `0.2`〜`0.5` です。dutyを上げると遠距離まで届きやすくなりますが消費電力が増え、近距離では高すぎると受信側が飽和してかえって安定性が落ちます。下げると距離は犠牲になりますが省電力になります。最適値は距離に依存するため、一般的な推奨値は `0.33` です。

**TXメモリブロック。** 送信中、RMTドライバは割り込みでチャネルを補充して次のシンボルを供給します。他の割り込み処理が長時間その補充をブロックすると、チャネルが枯渇して送信波形が壊れます。メモリブロック数を増やすと補充間隔が伸びるため、割り込み遅延への耐性が上がります。これは無線（Wi-Fi/BLE）を使用するシングルコアのESP32-C系で特に問題になりやすい点です。枯渇が起きる場合は、ブロック数を増やすか、`setPhaseAlignedCarrier(false)` でハードウェアキャリアにフォールバックしてください（送信精度は落ちますがシンボル数が大幅に少なく、安定を保ちやすくなります）。RMT TXメモリプールはアドレサブルRGB LEDなど他の用途と共有され、SoCごとにブロック数の上限があるため、すべてを1チャネルに割り当てるのは推奨しません。

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
| [06_ac_learn](examples/06_ac_learn) | エアコン学習。RAWキャプチャ・デコード結果・再送用コードを出力 |
| [07_ac_send](examples/07_ac_send) | PanasonicのACフレームをゼロから組み立てて送信 |

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
