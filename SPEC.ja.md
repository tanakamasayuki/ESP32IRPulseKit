# ESP32IRPulseKit 外部仕様

> English: [SPEC.md](SPEC.md)

ESP32IRPulseKit は、ESP32 Arduino Core 3.x / ESP-IDF 5.x の新RMTドライバを使うIRリモコン送受信ライブラリです。

本仕様書は利用者から見えるAPI契約を定義します。実装方針、スコアリングの考え方、ログ方針などは [DESIGN.ja.md](DESIGN.ja.md) を参照してください。

## 1. 対象

- 対象環境: ESP32 Arduino Core 3.x 以降
- 対象言語: Arduino Core 3.x 標準のC++環境
- 名前空間: `esp32irpk`
- 時間単位:
  - APIのprotocol specはマイクロ秒、`*_us`
  - RAW配列はtick、`1 tick = 10us`

## 2. 基本データ型

### 2.1 RAW

RAWはmark/spaceが交互に並ぶtick配列です。先頭はmarkです。

```cpp
namespace esp32irpk {

struct IRRawTickView {
  const uint16_t* ticks = nullptr;
  size_t len = 0;
};

struct IRRawTickBuffer {
  uint16_t* ticks = nullptr;
  size_t capacity = 0;
  size_t len = 0;
};

}
```

`IRRawTickView` は内部バッファを参照する場合があります。`IRReceiver::read()` で返されたviewは、次回の `read()` または `end()` まで有効です。

### 2.2 BITS

`IRDecodedBits` は、protocol判定後の正規化ビット表現です。

```cpp
namespace esp32irpk {

enum class IRFrameType : uint8_t {
  NORMAL = 0,
  REPEAT = 1,
};

struct IRDecodedBits {
  IRProtocolID protocol_id = IRProtocolID::UNKNOWN;
  IRFrameType frame_type = IRFrameType::NORMAL;
  uint16_t bit_length = 0;
  uint64_t bits = 0;

  bool isRepeat() const;
};

}
```

- `bit_length` は `0..64` です。
- 通常フレームは `frame_type == NORMAL` です。
- repeatフレームは `frame_type == REPEAT` です。repeat時は `bit_length == 0`、`bits == 0xffffffffffffffff` を基本表現とします。
- `bits` のビット順は `IRProtocolSpec::lsb_first` に従います。これは空中のビット送出順を
  `bits` のどのビット位置へ対応づけるかを表します。
  - `true`: 最初に送信するビットが `bits` の bit 0（LSB）。
  - `false`: 最初に送信するビットが `bit_length` の最上位ビット（`bits` の bit `bit_length - 1`）。
  これは各プロトコルの実際の送出順を反映します。NEC・Sony・JVC・Samsung（SAMSUNG32）・
  AEHA（Kaseikyo/Panasonic を含む）は LSB-first、RC5/RC6 と SAMSUNG36 は MSB-first で送出します。
  エンコーダとデコーダは対称にこのフラグを扱うため、自前TX→自前RX のラウンドトリップは
  常に同じ `bits` を復元します。
  - 他ライブラリは同じ空中信号を逆端からインデックスすることがあり（例：IRremoteESP8266 は
    NEC の最初に送るビットを整数の MSB 側へ格納）、波形が同一でも整数値はビット反転して見え
    ます。これは非互換ではなく表現差で、`compat_matrix` テストでは `bit_order = reversed`
    として記録します。

### 2.3 Protocol Spec

```cpp
namespace esp32irpk {

struct IRPulseUs {
  uint32_t mark_us = 0;
  uint32_t space_us = 0;
};

enum class IRProtocolScheme : uint8_t {
  UNKNOWN = 0,
  SPACE_ENC = 1,
  BIPHASE = 2,
};

enum class IRProtocolFamily : uint8_t {
  UNKNOWN = 0,
  NEC_LIKE = 1,
  AEHA = 2,
  SONY = 3,
  RC5 = 4,
  RC6 = 5,
};

struct IRProtocolSpec {
  IRProtocolID protocol_id = IRProtocolID::UNKNOWN;
  char name[16] = {};
  IRProtocolScheme scheme = IRProtocolScheme::UNKNOWN;
  IRProtocolFamily family = IRProtocolFamily::UNKNOWN;

  IRPulseUs header{};
  IRPulseUs one{};
  IRPulseUs zero{};
  IRPulseUs trailer{};

  uint32_t gap_threshold_us = 0;
  uint32_t idle_threshold_us = 0;
  uint32_t carrier_hz = 0;

  bool lsb_first = true;

  uint16_t bit_length = 0;
  uint16_t min_bit_length = 0;
  uint16_t max_bit_length = 0;

  bool has_repeat = false;
  IRPulseUs repeat_header{};
  uint32_t repeat_gap_us = 0;
  int8_t default_repeat_count = 0;

  uint16_t bit_tol_pct = 25;
  uint8_t order = 0;
};

}
```

- `gap_threshold_us`: decode時のフレーム分割に使うgap下限です。`0` はgap分割なしです。
- `idle_threshold_us`: RMT idle thresholdの推奨値です。`0` の場合はreceiver設定値を使います。
- `carrier_hz`: 送信時の推奨carrier周波数です。`0` の場合はライブラリ既定値を使います。
- 標準protocolでも推奨carrierを確認していないものは `0` のままにします。すべてのprotocolに38kHzを一律設定することはしません。
- `name`: 表示用の固定長コピー文字列です。最大15文字 + 終端NULです。`addProtocol()` は `IRProtocolSpec` を値としてコピーするため、外部文字列の寿命管理に依存しません。
- 固定長protocolでは `bit_length` を使います。
- 可変長protocolでは `min_bit_length..max_bit_length` を使います。`0` の場合は `bit_length` を下限/上限として扱います。
- `default_repeat_count` は `repeat_count < 0` の送信で使う既定の追加送信回数です。`0` は1回だけ送信、`2` は合計3回送信です。
- `bit_tol_pct` は、そのprotocolで「良好な信号」とみなす標準的な誤差率です。decode候補を必ず棄却する絶対上限ではありません。仕様値から外れたmark/spaceでも、フレーム形状とbit分類が十分に成立する場合は候補として残り、誤差分はscoreへ反映されます。
- `order` は登録順です。利用者が設定する必要はありません。

### 2.4 Decode Result

```cpp
namespace esp32irpk {

inline constexpr size_t kDefaultMaxDecodeCandidates = 4;

enum class IRResultFlags : uint8_t {
  NONE = 0,
  DECODE_SKIPPED = 1 << 0,
  RAW_TRUNCATED = 1 << 1,
  RMT_OVERFLOW = 1 << 2,
};

struct IRDecodeCandidate {
  IRProtocolID protocol_id = IRProtocolID::UNKNOWN;
  char name[16] = {};
  uint8_t order = 0;
  int16_t score = 0;
  size_t consumed_len = 0;
  IRDecodedBits decoded{};
};

template <size_t MaxCandidates = esp32irpk::kDefaultMaxDecodeCandidates>
struct IRReceiveResult {
  IRRawTickView raw{};
  IRResultFlags flags = IRResultFlags::NONE;
  uint8_t count = 0;
  IRDecodeCandidate candidates[MaxCandidates]{};

  const IRDecodeCandidate* candidate() const;
  const IRDecodedBits* bits() const;

  bool hasFlag(IRResultFlags bit) const;
  bool decodeSkipped() const; // hasFlag(DECODE_SKIPPED)
  bool truncated() const;     // hasFlag(RAW_TRUNCATED)
  bool rmtOverflow() const;   // hasFlag(RMT_OVERFLOW)
};

struct IRRxStats {
  uint32_t queue_overflow_count = 0;
  uint32_t rmt_overflow_count = 0;
  uint32_t raw_truncated_count = 0;
};

}
```

- `candidates` はscore降順で返ります。同scoreの場合は登録順が早いものを優先します。
- `candidate()` は最上位候補、`bits()` は最上位候補のBITSを返します。候補がない場合は `nullptr` です。
- `hasFlag(bit)` は結果フラグを判定します。`decodeSkipped()`・`truncated()`・`rmtOverflow()` は3フラグの名前付きショートカットです。
- `score` は受信波形がprotocol specへどれだけ近いかを表す相対値です。複数protocolに似た波形は複数候補として残ることがあり、利用者は通常、最上位候補とscore差を見ます。scoreの絶対値や計算式は互換性契約ではありません。
- `DECODE_SKIPPED`: RAW-only設定などでdecodeしなかったことを示します。
- `RAW_TRUNCATED`: RAWが内部上限を超えて切り詰められたことを示します。
- `RMT_OVERFLOW`: RMT受信でoverflowが発生した可能性を示します。

### 2.5 Decode判定とscore

decodeは、完全一致だけを受け入れる判定ではありません。実IR受信では受信モジュール、距離、角度、carrier duty、外乱光によりmark/spaceが系統的に伸縮するため、ESP32IRPulseKit は次の方針で候補化とscore付けを行います。

- 明らかな別物は早期除外します。例: headerが大きく異なる、mark/spaceの並びが壊れている、bit数が範囲外、repeat形状が成立しない、必要なgap/trailerが成立しない。
- `bit_tol_pct` 内の波形は高品質な一致として扱います。
- `bit_tol_pct` を少し超える波形でも、protocolの符号化規則上bit値を分類できる場合は候補として残します。
- SPACE_ENC系では、0/1のspace長が十分に離れているprotocolについて、strictな許容窓だけでなく近い期待spaceへの分類を使えます。分類後のnominalからの誤差はscoreへ累積します。
- BIPHASE系では、half-bit/grid構造が成立する範囲で候補化し、単位幅からのずれをscoreへ反映します。
- 似たprotocolが同じRAWから候補に残ることは正常です。最終的な優先順位はscore降順、同scoreなら登録順です。

このため、protocol仕様上の許容値（例: NECのbit timing ±25%）は「候補に残す最大範囲」ではなく「良好一致の基準」です。候補化のための内部許容は実装詳細ですが、bit分類が曖昧になるほど外れた波形や、フレーム形状が成立しない波形は候補から除外されます。

## 3. 内蔵Protocol

内蔵protocol specは `esp32irpk::specs` にあります。

```cpp
esp32irpk::specs::NEC
esp32irpk::specs::AEHA
esp32irpk::specs::SONY12
esp32irpk::specs::SONY15
esp32irpk::specs::SONY20
esp32irpk::specs::SAMSUNG32
esp32irpk::specs::SAMSUNG36
esp32irpk::specs::JVC
esp32irpk::specs::RC5
esp32irpk::specs::RC6_M0_16
esp32irpk::specs::RC6_M6_32
```

`IRProtocolID` は波形の近さだけでなく、`IRDecodedBits.bits` の論理解釈が互換かどうかで分けます。たとえばSamsung 32bitと36bitは別IDです。

### 3.1 追加候補protocol（未実装）

現状の11でリモコン用途の大半をカバーするため、以下は「必須」ではなく**候補**です。
学習＆そのまま再送（raw capture/replay）だけが目的なら、生tick経路で任意protocolを
扱えるので追加は不要です。意味のあるbitsへのdecode／コード生成が必要な場合にのみ
価値があります。実用度で3段階に仕分けます。

**Tier A: 既存decoderで実質対応済み（追加はID/ラベルのマップのみ）**

| 候補 | 系統 | 備考 |
|---|---|---|
| NEC extended / NEC2 | NEC_LIKE | 16bitアドレス系。既存NECで生bitsは取得可 |
| Denon-Kaseikyo / JVC-Kaseikyo / Sharp-Kaseikyo / Mitsubishi-Kaseikyo | AEHA | メーカIDが違うだけ。AEHA decoderが正しいbitsを返す |

→ `IRProtocolID` の追加とdecode結果のラベル付けが中心。新規decode実装は不要。

**Tier B: 追加の費用対効果が高い（実機が現存・小〜中の実装）**

| 候補 | 系統 | キャリア | 備考 |
|---|---|---|---|
| Pioneer | NEC_LIKE | 40kHz | NECを2フレーム送出。AVアンプ |
| Onkyo | NEC_LIKE | 38kHz | NEC系パラメータ違い |
| Sharp | 独自(space-enc) | 38kHz | AQUOS等。expansion/checkの反転送出が要対応 |
| Denon | 独自(space-enc) | 38kHz | 旧Denon系（Kaseikyo側で足りる場合も多い） |
| RC6A（可変長） | BIPHASE | 36kHz | MCE/Windowsリモコン・Xbox360 IR・一部STB |

**Tier C: 基本スキップ（特定ニーズが出てから個別対応）**

- RCMM, RECS80, Nokia NRC17, Grundig, Nubert, XMP, F12, G.I.Cable, Whynter
- Lego Power Functions / MagiQuest（玩具・施設系）
- Bang & Olufsen: **455kHzキャリア**。一般的な38kHz TSOPでは受信できずハード的に別物。**非対応のままが無難**
- エアコン／ヒートポンプ系（Daikin / Mitsubishi-AC / Panasonic-AC / Gree / Coolix 等）:
  ボタン1つで数十〜数百bitの状態を丸ごと送るため、汎用の64bit単一フレーム
  decode/encodeモデルには収まらない。汎用デコーダではなく別レイヤ（`esp32irpk::ac`）で
  扱うので汎用候補ではない（§11参照。Panasonic は実装済み）

方針：追加するなら **Tier B まで**を上限とし、Tier C は需要が出た時点で個別に検討する。
Tier A は「対応protocol数を増やす」目的に限り低コストで足せる。

## 4. Protocol登録と初期化

`IRReceiver` と `IRSender` は、protocol specを内部にコピーして保持します。

以下の設定は `begin()` 前のみ有効です。`begin()` 後に呼ぶと `false` を返します。

- `setPin()`
- `setInverted()`
- `addProtocol()`
- `clearProtocols()`
- `setDecodeCandidates()`
- `setIdleThresholdUs()`
- `setScoreThreshold()`
- `setMaxRxSymbols()`

### 4.1 Protocol選択

`addProtocol()` はprotocol specを登録します。`begin()` 前のみ有効です。

- specは値としてコピーされます。
- 同じ `protocol_id` のspecが既に登録されている場合、`addProtocol()` はそれを置換します。なければ追加します。
- 内蔵protocolをカスタマイズ／上書きするには、`esp32irpk::specs::*` のコピーを書き換えて登録します。同じ `protocol_id` の内蔵protocolが置換されます。

`clearProtocols()` は登録済みprotocolをすべて削除します。

### 4.2 自動登録

`begin()` 時の自動登録は、senderとreceiverで挙動が異なります:

- `IRSender`: 未登録の `protocol_id` を持つ内蔵protocolがすべて追加されます（top-up）。そのためsenderは常に内蔵protocol一式を備え、specの登録は1件のカスタマイズ／上書きであって、対象集合を絞る操作ではありません。
- `IRReceiver`: decode候補数が1以上で、protocolが1つも登録されていない場合に内蔵protocolをすべて登録します。1つでも登録されている場合は、登録済みのものだけを使います（decode候補集合を絞れます）。
- `IRReceiver`: decode候補数が0の場合はRAW-only扱いで、protocolを登録しません。

`addProtocol()` で登録したspecは登録順の相対順序を保持し、`begin()` のtop-upで追加された内蔵protocolはその後ろに並びます。この順序は2.4節のタイブレークに使われます。

### 4.3 Decode候補数

`setDecodeCandidates(n)` は `IRReceiver` が `read()` ごとに保持するdecode候補数を設定します。`begin()` 前のみ有効です。

- 範囲は `0..MaxCandidates`（`IRReceiver` のテンプレート引数）。`begin()` 後、または `n > MaxCandidates` の場合は `false` を返します。
- 既定値は `MaxCandidates` です。
- `n >= 1`: `read()` はデコードし、スコア上位 `n` 件までを2.4節の順序で保持します。
- `n == 0`: RAW-only扱い。`read()` は RAW を返して `DECODE_SKIPPED` を立て、デコードは行わず、`begin()` で内蔵protocolも登録しません（4.2節参照）。RAW-onlyはこの方法でのみ選択され、protocolを未登録にしただけでは選ばれません。

### 4.4 Idle閾値

`setIdleThresholdUs()` の未指定値は `30000us` です。decode有効時は、登録済みprotocolの `idle_threshold_us` の最大値とreceiver設定値の大きい方を使います。

### 4.5 RXキャプチャ容量

`setMaxRxSymbols(symbols)` は1回のキャプチャが保持できるRMTシンボルの最大数を設定します。`begin()` より前でのみ有効です。

- 既定値は通常の短いリモコンフレーム向けのサイズです。容量を超えるキャプチャはtruncateされ `RAW_TRUNCATED` が立ちます。
- エアコンフレーム（§11）のような長い波形にはより大きい値が必要です。`setIdleThresholdUs()` と併せて引き上げ、バースト全体を1キャプチャに収めます。
- 容量を大きくするとreceiverごとにRAMを消費するため、グローバル既定ではなくオプトインです。

## 5. IRReceiver

```cpp
namespace esp32irpk {

template <size_t MaxCandidates = esp32irpk::kDefaultMaxDecodeCandidates>
class IRReceiver {
public:
  explicit IRReceiver(int gpio);
  IRReceiver(int gpio, bool inverted);

  bool setPin(int gpio);
  bool setInverted(bool inverted);

  bool setDecodeCandidates(uint8_t n);
  bool setIdleThresholdUs(uint32_t us);
  bool setScoreThreshold(int16_t score);
  bool setMaxRxSymbols(size_t symbols);

  bool addProtocol(const IRProtocolSpec& spec);
  bool clearProtocols();

  bool begin();
  void end();

  bool read(IRReceiveResult<MaxCandidates>& out);
  bool decode(const IRRawTickView& raw, IRReceiveResult<MaxCandidates>& out) const;

  IRRxStats stats() const;
  void resetStats();
};

}
```

### 5.1 read

- `true`: 受信RAWを取得しました。`out.raw.len > 0` です。
- `false`: 取得可能なRAWがありません。`out` の内容は未定義です。

decode候補数が0の場合、`read()` はRAWのみ返し、`DECODE_SKIPPED` を立てます。

decode候補数が1以上の場合、`read()` はRAWとdecode候補を返します。候補がない場合でもRAW取得に成功していれば `true` を返し、`out.count == 0` になります。

### 5.2 decode

`decode(raw, out)` は登録済みprotocolでRAWをdecodeします。RMT受信は行いません。

- `true`: 1件以上のdecode候補があります。
- `false`: decode候補がありません。

## 6. IRSender

```cpp
namespace esp32irpk {

class IRSender {
public:
  explicit IRSender(int gpio);
  IRSender(int gpio, bool inverted);

  bool setPin(int gpio);
  bool setInverted(bool inverted);
  bool setCarrierHz(uint32_t hz);
  bool clearCarrierHz();
  bool disableCarrier();
  bool setCarrierDuty(float duty);
  bool setPhaseAlignedCarrier(bool enable);
  bool setTxMemBlocks(uint8_t blocks);

  bool addProtocol(const IRProtocolSpec& spec);
  bool clearProtocols();

  bool begin();
  void end();

  bool send(const IRRawTickView& raw, int8_t repeat_count = -1);
  bool send(const IRRawTickView* raw, int8_t repeat_count = -1);
  bool send(const IRDecodedBits& decoded, int8_t repeat_count = -1);
  bool send(const IRDecodedBits* decoded, int8_t repeat_count = -1);

  bool encode(const IRDecodedBits& decoded, IRRawTickBuffer& out_raw);
};

}
```

### 6.1 RAW送信

`send(raw)` はRAW tick配列をそのまま送信します。

- `begin()` 前は `false` を返します。
- `raw.ticks == nullptr` または `raw.len == 0` は `false` を返します。
- pointer overloadは `nullptr` を渡すと `false` を返します。
- RAW送信では `repeat_count < 0` は `0` と同じです。
- carrierは `setCarrierHz()` で明示固定されていればその値、なければライブラリ既定値 `38000` を使います。

### 6.2 BITS送信

`send(decoded)` は `IRDecodedBits` をRAWへencodeして送信します。

- `begin()` 前は `false` を返します。
- `decoded.protocol_id` に対応するprotocol未登録なら `false` を返します。
- 固定長protocolでは `decoded.bit_length == spec.bit_length` が必要です。
- 可変長protocolでは `decoded.bit_length` が `min_bit_length..max_bit_length` の範囲内である必要があります。
- repeatフレームは `spec.has_repeat == true` のprotocolでのみ送信できます。
- `repeat_count < 0` の場合は `spec.default_repeat_count` を使います。
- `repeat_count >= 0` の場合は呼び出し値を使います。
- carrierは `setCarrierHz()` の明示固定値、`spec.carrier_hz`、ライブラリ既定値 `38000` の順で解決します。
- 標準protocolの推奨値: NEC/AEHA/Samsungは38kHz、JVCは37.9kHz、Sony SIRCは40kHz、RC5/RC6は36kHzです。

### 6.3 carrier設定

`setCarrierHz(hz)` は送信carrier周波数をsender単位で明示固定します。

- `hz == 0` は `clearCarrierHz()` と同じです。
- 許可範囲は `20000..60000` Hzです。範囲外は `false` を返します。
- `begin()` 前でも後でも呼べます。
- `begin()` 後の変更は次回送信から反映されます。可能な場合はRMT TX channelにも即時反映します。
- 送信中に呼んだ場合は `false` を返します。

`clearCarrierHz()` はsender単位の明示固定を解除します。

- RAW送信はライブラリ既定値 `38000` に戻ります。
- BITS送信はprotocol specの `carrier_hz` を再び使います。

`disableCarrier()` はキャリア変調なしのソリッドmarkで送信します。`begin()` 前後どちらでも呼べ、送信中は `false` を返します。

`setCarrierDuty(duty)` はキャリアのオン時間比率を設定します。既定値は約 `0.33` です。

- 許可範囲は `0 < duty < 1` です。範囲外は `false` を返します。
- `begin()` 前後どちらでも呼べ、送信中は `false` を返します。
- 実用範囲はおおよそ `0.2`〜`0.5` です。dutyを上げると遠距離まで届きやすくなりますが消費電力が増え、近距離では高すぎると受信側が飽和して安定性が落ちます。下げると距離は犠牲になりますが省電力になります。最適値は距離に依存するため、一般的な推奨値は `0.33` です。

### 6.5 キャリア生成とTXチャネル

`setPhaseAlignedCarrier(enable)` はキャリアの生成方式を選びます。TXチャネルの構成を固定するため `begin()` より前でのみ有効で、`begin()` 後は `false` を返します。

- `true`（既定）: 位相整合・シンボルエンコードのキャリア。各markを位相0から始まる整数個のキャリアサイクルとして出力するため、復調後のmarkがフレーム間で安定します。他ライブラリとの相互運用に適します。キャリア周期は整数us（チャンネルは1us分解能）に量子化されるため、出力周波数は最近傍の `1/N us` に丸められます: 38kHz → 26us ≈ 38.46kHz（+1.2%）、36kHz → 28us ≈ 35.71kHz（−0.8%）、40kHz → 25us（誤差なし）。誤差はIR受光モジュールの通過帯域内で、周期ごとのディザリングは行いません。
- `false`: フリーランのハードウェアキャリア（`rmt_apply_carrier`）。RMTシンボル数は大幅に少ない一方、markごとに位相がリセットされないため、復調後のmarkが±1キャリアサイクル揺れます。

`setTxMemBlocks(blocks)` はTXチャネルのRMTメモリブロック数を設定します（1ブロック = `SOC_RMT_MEM_WORDS_PER_CHANNEL` シンボル）。`begin()` より前でのみ有効です。

- `0` はライブラリ既定（1ブロック）を使います。
- 送信中、RMTドライバは割り込みでチャネルを補充します。他の割り込みが長時間その補充をブロックするとチャネルが枯渇し、波形が壊れます。ブロック数を増やすと補充間隔が伸び、割り込み遅延への耐性が上がります。無線を使用するシングルコアのESP32-C系で特に効きます。
- 枯渇が起きる場合は、ブロック数を増やすか、`setPhaseAlignedCarrier(false)` でハードウェアキャリアにフォールバックします（送信精度は低下、シンボル数は大幅減）。
- RMT TXメモリプールはアドレサブルRGB LEDなど他の用途と共有され、SoCごとにブロック数の上限があるため、すべてを1チャネルに割り当てるのは推奨しません。

キャリアとタイミングモデルの全体は [DESIGN.ja.md](DESIGN.ja.md) §8・§12 を参照してください。

### 6.6 encode

`encode(decoded, out_raw)` はBITSをRAWへ変換します。送信は行いません。

- `out_raw.ticks` と `out_raw.capacity` は呼び出し側が用意します。
- 成功時は `out_raw.len` に生成tick数を設定します。
- 失敗時は `false` を返します。

## 7. Frame型

protocol別のFrame型は `esp32irpk::frames` に置きます。Frame型は、protocol固有の論理値と `IRDecodedBits` の変換だけを担当します。

代表的な形:

```cpp
namespace esp32irpk::frames {

struct NECFrame {
  uint16_t address = 0;
  uint8_t command = 0;
  bool is_repeat = false;

  static NECFrame fromBits(const esp32irpk::IRDecodedBits& in);
  esp32irpk::IRDecodedBits toBits() const;
};

}
```

Frame型はdecode/encode本体ではありません。decode/encodeは `IRProtocolSpec` と codec が担当します。

すべての内蔵protocolのFrame型は `fromBits()` と `toBits()` の両方を備えます。デコード結果の確認と再送を対称に行えます。

## 8. BITS helper

すべての内蔵protocolには、`IRDecodedBits` を返す小さなhelperが `esp32irpk::bits` にあります。helperは各protocolのFrameフィールドに対応し、最短の生成経路です。

```cpp
esp32irpk::IRDecodedBits bits = esp32irpk::bits::nec(0x00ff, 0x34);
tx.send(bits);
```

| helper | 対象 |
|---|---|
| `bits::nec(address, command)` / `bits::necRepeat()` | NEC |
| `bits::aeha(data, bit_length)` | AEHA（可変長） |
| `bits::sony12(data)` / `bits::sony15(data)` / `bits::sony20(data)` | Sony SIRC |
| `bits::samsung32(address, command)` / `bits::samsung36(address, command)` | Samsung |
| `bits::jvc(address, command)` | JVC |
| `bits::rc5(data)` / `bits::rc6m0(data)` / `bits::rc6m6(data)` | RC5 / RC6 の生ペイロード |

helperは送信APIではありません。送信は常に `IRSender::send()` が担当します。

## 9. 戻り値と安全性

- 例外は使いません。
- 成否は `bool` で返します。
- begin前/後制約に違反した呼び出しは `false` を返します。
- `read(false)` 時の `out` は未定義です。
- APIはISRから呼び出してはいけません。
- 同一インスタンスへの複数スレッド同時呼び出しは未定義です。

## 10. 最小利用例

```cpp
#include <ESP32IRPulseKit.h>

esp32irpk::IRReceiver<> rx(4, true);
esp32irpk::IRSender tx(5);

void setup() {
  Serial.begin(115200);
  rx.begin();
  tx.begin();
}

void loop() {
  esp32irpk::IRReceiveResult<> result;
  if (!rx.read(result)) {
    return;
  }

  if (const esp32irpk::IRDecodedBits* bits = result.bits()) {
    tx.send(bits);
  }
}
```

## 11. エアコン対応

エアコン／ヒートポンプのリモコンは、ボタン1つで多バイトの状態をまとめて送ります（しばしば100〜300+ bit、ベンダ固有レイアウト＋チェックサム、1押下で複数フレームのこともある）。これは汎用codecの64bit単一フレーム（`IRDecodedBits`）モデルに収まらないため、AC対応は `esp32irpk::ac` 配下の**別レイヤ**として RAW tick 経路上で実装します。`IRDecodedBits`・候補スコアラ・`IRProtocolID` には一切触れず、ACベンダは自動登録もされません。

### 11.1 学習＆再送（ベンダ非依存）

任意のAC波形は、デコードせずにキャプチャして再送できます。

- RAWのみモード（`setDecodeCandidates(0)`）で受信。`setMaxRxSymbols()` をフレームに足るサイズに、`setIdleThresholdUs()` をフレーム内ギャップを跨げるサイズにして、バースト全体を1キャプチャに収める。
- `read()` はバースト全体を1つの `IRRawTickView` として返す（RAWのみモードは分割しない）。
- キャプチャしたRAWを `IRSender::send(const IRRawTickView&)` で再送する。ACフレームは位相整合キャリア（既定。§11.3）を使う——ベンダ横断で安全な選択で、一部（例: Gree）はハードウェアキャリアだとフレームが落ちる。

### 11.2 デコード＆エンコード（ベンダ別）

意味あるフィールドへのデコードとフレーム再生成はベンダごとに扱います。各ベンダは自分の名前空間 `esp32irpk::ac::<Vendor>` に**同じ構造**（`Mode` / `Fan` / `Frame`）を持ち、RAW tick とバイト構造の論理状態を変換します。汎用の `frames::*` の `fromBits`/`toBits` パターンを踏襲しつつ、RAWベース・バイト幅にしたものです。

`Mode`・`Fan` は単一の共通enumではなく **ベンダ別enum** です。各ベンダが実際に対応する値だけを持つため、非対応の設定は名前を付けることすらできません。共通メンバは命名規約（`AUTO`/`COOL`/`HEAT`/`DRY`/`FAN`、対応する場合のfan段）を揃え、どのベンダも同じ読み口になります。名前の**構造**は全ベンダ共通（`ac::<Vendor>::Mode::COOL`）で、**メンバ集合**がベンダ固有です。

```cpp
namespace esp32irpk::ac {

enum class AcVendor : uint16_t {
  UNKNOWN = 0,
  PANASONIC = 1,
  GREE = 2,
  // ベンダは順次追加
};

namespace Panasonic {

// ベンダ別: Panasonicが対応する値だけ。共通メンバは命名規約に従う。
// このenumはPanasonicが持たない値を許さない。
enum class Mode : uint8_t { AUTO = 0, COOL, HEAT, DRY, FAN };
// Arduinoが LOW/HIGH をマクロ定義するため、fan段は `_SPEED` サフィックスを使う。
// QUIET/POWERFUL は風量ではなく快適モード（fanはautoのまま byte21 フラグ）。風量と
// 排他なので同じ Fan セレクタの値として持つ。
enum class Fan : uint8_t {
  AUTO = 0, MIN_SPEED, LOW_SPEED, MED_SPEED, HIGH_SPEED, MAX_SPEED, QUIET, POWERFUL
};
// Panasonic固有: 垂直ルーバー位置（fanバイト下位ニブル）。AUTOはスイング。
enum class Louver : uint8_t { AUTO = 0xF, P1 = 1, P2, P3, P4, P5 };

struct Frame {
  static constexpr size_t kMaxTicks = /* ベンダフレームの上限 */;

  uint8_t bytes[kBytes] = {}; // 復号した生の状態（中間形式）
  uint16_t byte_length = 0;
  bool checksum_ok = false;
  // この波形フォーマットにモデル分岐がある場合（例 Gree の YBOFB/YAW1F）、Frame は
  // `Model model` フィールドも持つ。fromRaw が設定し、toRaw/アクセサが従う。
  // 単一モデルのフォーマットでは持たない。後述「分岐の2軸」を参照。

  // `bytes` 上の論理アクセサ
  bool power() const;          void setPower(bool on);
  Mode mode() const;           void setMode(Mode m);
  float temperatureC() const; void setTemperatureC(float c); // 0.5℃刻み、get/set対称
  Fan fan() const;             void setFan(Fan f);
  // Panasonic固有: temperatureC()/setTemperatureC は 0.5℃を含む float で対称。
  // halfDegree() は +0.5 ビットだけを見る簡便アクセサ。
  bool halfDegree() const;
  Louver louver() const;       void setLouver(Louver v);

  static bool fromRaw(const esp32irpk::IRRawTickView& raw, Frame& out);
  static bool fromBytes(const uint8_t* state, size_t len, Frame& out); // デコード済み状態から復元
  bool toRaw(esp32irpk::IRRawTickBuffer& out) const;
  void printTo(Print& out) const; // 診断ダンプ: 共通＋ベンダ固有＋hex
};

} // namespace Panasonic

// 便利関数: 任意ベンダの frame をエンコードして1呼び出しで送信する。
template <class Frame>
bool send(esp32irpk::IRSender& tx, const Frame& frame);

// 全内蔵ACベンダをRAWキャプチャに対して試し、一致したベンダを返す（無ければUNKNOWN）。
// Print が非nullなら一致フレームを printTo() でダンプする（不一致時は注記を出力）。
AcVendor decodeAny(const esp32irpk::IRRawTickView& raw, Print* out = nullptr);

// 一致フレームをデコード済み状態バイトから復元する貼り付け用C++を出力する（RAW tick
// ダンプの、コンパクトで完全一致な代替）。一致ベンダを返す（無ければUNKNOWN・何も出力
// しない＝RAWにフォールバック）。
AcVendor printSendSnippet(const esp32irpk::IRRawTickView& raw, Print& out);

// 論理 setter による編集可能テンプレート（f.setMode(...); f.setTemperatureC(...); ...）を
// 出力する。値を変えて再送する実用形だが lossy（setter の無いフィールドは既定値に戻り
// 完全一致しない。忠実さが要るなら printSendSnippet）。
AcVendor printSetterSnippet(const esp32irpk::IRRawTickView& raw, Print& out);

}
```

- `Frame::fromRaw(raw, out)` はRAW tickを状態バイトへ復号し、ベンダのチェックサムを検証します。そのベンダのフレームでない場合は `false` を返し、チェックサムの可否は `out.checksum_ok` で別に報告します。
- `Frame::fromBytes(state, len, out)` は `kBytes` のデコード済み状態から、RAW tick を経由せずにフレームを復元します（`fromRaw` 同様にモデル判定し `checksum_ok` を報告）。`len` は `Frame::kBytes` と一致が必要。RAW replay のコンパクトで完全一致な対応物で、`fromBytes` → `toRaw`/`ac::send` でキャプチャ元をバイト単位で再現します（再生成されるのは決定的な署名・プリアンブル・マーカー・checksum のみ）。`ac::printSendSnippet` がこれを使った貼り付けスニペットを出力します。（論理 setter から組み直すと setter の無いフィールド（例: タイマー）がテンプレ既定に戻り完全一致しません。）
- `Frame::toRaw(out)` はチェックサムを再計算し、状態を caller提供の `IRRawTickBuffer` にRAW tickとして書き出します。結果は `IRSender::send(const IRRawTickView&)` で送信します。`model` がフィールドマップ未実装のモデルを指す場合は `false` を返します（未対応モデルは、実装済みモデルのレイアウトを黙って出力せず、エンコード失敗にする）。`ac::send` もこれを伝播して `false` を返します。
- `ac::decodeAny(raw, out)` は全内蔵ACベンダを登録順にRAWキャプチャへ当て、一致した `AcVendor` を返します（無ければ `UNKNOWN`）。`out` が非nullなら一致フレームを `printTo()` でダンプします。返すのはベンダ識別のみで、デコード済みフレームは返しません（各ベンダの `Frame` は不均質な型のため）— フィールド参照や再エンコードが要るなら該当ベンダを個別にデコードします。カスケードをここに集約することで、学習/ダンプ経路が新ベンダを自動的に拾います。
- `ac::printSendSnippet(raw, out)` は一致ベンダをデコードし、`Frame::fromBytes` でフレームを組み直して送信する貼り付け用C++を出力します — RAW tick 配列（数百個）に対し 27/18/8 バイトで済む、コンパクトかつ完全一致な代替です。一致した `AcVendor` を返し、無ければ `UNKNOWN`（何も出力しない＝RAWスニペットにフォールバック）。`decodeAny` と対でベンダリストを一元化します。
- `ac::printSetterSnippet(raw, out)` は代わりに論理 setter による**編集可能**テンプレート（`f.setMode(...)`・`f.setTemperatureC(...)`、enum 値は `toString`）を出力します — 「値を変えて再送する」実用形です。ただし **lossy**：setter の無いフィールド（タイマー・ベンダフラグ）はテンプレート既定値に戻るため完全一致しません（忠実さが要るなら `printSendSnippet`）。戻り値の規約は同じ。generic プロトコルには対応する `esp32irpk::debug::printFrameStructSnippet`（1行の `bits::*` ヘルパー＋編集可能な `frames::*Frame` 構造体）があります。
- `ac::send(tx, frame)` は1呼び出し版です。`Frame::kMaxTicks` のスタックバッファへエンコードして送信し、エンコード/送信失敗時は `false` を返します。バッファを自分で管理したい場合は `toRaw` + `IRSender::send()` を使います。送信機のキャリアモードは従来どおり別に設定します（ACでは位相整合の既定を使う。§11.3参照）。
- 中間形式はバイト配列です。power / mode / temperature / fan などの論理フィールドはそのバイト上のアクセサです。
- `Frame::printTo(Print& out)` は診断用ダンプです。共通の `power/mode/temp/fan/checksum` 行、ベンダ固有フィールド（louver / swing / vane）、状態全体のhexを任意の Arduino `Print`（例 `Serial`）へ書き出します。enumフィールドは生コードで出力。学習/ダンプ用スケッチ向けの便宜機能で、encode/decode 契約の一部ではありません。
- どのベンダもこの同じ構造を自分の `esp32irpk::ac::<Vendor>` 名前空間で提供します。各ベンダは enum（`Mode`/`Fan` と swing/louver/vane 系）を列挙子名そのまま（例 `"MIN_SPEED"`）へ変換する `toString(...)` オーバーロードも提供し、`printTo` で使うほかログにも利用できます（不明値は `"?"`）。bare 名が再利用しやすい形で、コード生成時は `esp32irpk::ac::<Vendor>::<Enum>::` を前置します。これらは便宜ヘルパーでコア契約の一部ではありません。
**分岐の2軸。** あるベンダのリモコンは独立した2つの軸で異なり、本レイヤーはそれぞれ別の表現で扱います。

- **フォーマット（波形プロトコル）** — フレーム長・タイミング・ヘッダ・チェックサムが違うものは本質的に別波形で別パーサが要るため、**別の `Frame` 型**にします。複数の波形フォーマットを持つベンダは、自分の名前空間にフォーマットごとに1つの `Frame` 型を持ちます（例: 18バイトの「Mitsubishi AC」protocol は1つの型、より短い Mitsubishi 136 / 112 protocol は別の型になる）。
- **モデル** — 同一の波形フォーマットを共有し、モデル識別ビットや一部フィールド符号化だけが違う場合は、**単一の `Frame` 型の `Model` パラメータ**として扱います（別型にはしません）。`fromRaw` が捕捉バイト（フレーム長/チェックサム/モデルビット）からモデルを判定して `Frame::model` に記録し、`toRaw` とアクセサがそれに従います。モデル追加は加算的で、enum値とそのフィールド処理を足すだけ・API変更なし。

```cpp
namespace Gree {
enum class Model : uint8_t { YBOFB = 0, YAW1F, YX1FSF }; // 1つの波形フォーマットの分岐
struct Frame {
  // ...bytes / checksum_ok は上記と同じ...
  Model model = Model::YBOFB; // fromRaw が判定し、toRaw＋アクセサが従う
};
}
```

モデルを「モデルごとの別型」でなくパラメータにするのは、受信フレームが `fromRaw` で**自分のモデルを自分で解決**する必要があるからです（受信側がクラスを先に選べない）。一方、新しい波形**フォーマット**はパーサを共有できないため常に独自の `Frame` 型になります。

**ベンダ／フォーマット／モデル対応可否一覧。** 「対応」は実装済みかつ実機検証済み（IRremoteESP8266 と双方向＋HeatpumpIR の2系統目参照）。「予定」「未対応」のフォーマット／モデルは存在を把握しているが未実装で、対象モデルは実装前に決定し、ここで先に固定はしません。

| ベンダ | フォーマット（プロトコル） | フレーム | モデル | 状態 |
|---|---|---|---|---|
| Panasonic | Kaseikyo AC | 27バイト・2フレーム | JKE / DKE / NKE / LKE / RKR | **対応** |
| | | | CKP | 未対応 |
| | Panasonic-AC32 | 短縮32bit | — | 未対応 |
| Gree | Gree | 8バイト・2ブロック | YBOFB | **対応** |
| | | | YAW1F / YX1FSF | 未対応 |
| Mitsubishi | Mitsubishi AC | 18バイト | 単一 | **対応** |
| | Mitsubishi 136 | 17バイト | — | 未対応 |
| | Mitsubishi 112 | 14バイト | — | 未対応 |
| | Mitsubishi Heavy | 88 / 152bit | — | 未対応 |
| Fujitsu | Fujitsu AC | 長16バイト / 短7バイト | ARRAH2E | **対応** |
| | | | ARDB1 / ARJW2 / ARREB1E / ARRY4 / ARREW4E | 未対応 |
| Daikin | Daikin classic（ARC433） | 35バイト・3セクション | 単一 | **対応** |
| | Daikin2 / 216 / 160 / 176 / 128 / 152 / 64 / 312 | サイズ各種 | — | 未対応 |
| Toshiba | Toshiba AC | 9バイト | 標準 | **対応** |
| | 短（7バイトswing）/ 長（10バイト） | — | 未対応 |
| Samsung | Samsung AC | 14バイト、2セクション | 標準 | **対応**² |
| | 拡張（21バイトタイマ） | — | 未対応 |
| Sharp | Sharp AC | 13バイト | A907 | **対応**¹ |
| | | A705 / A903 | 未対応 |
| Kelvinator | Kelvinator | 16バイト、2ブロック | 標準 | **対応**³ |
| Midea | Midea | 48ビット（6バイト）、2コピー | 標準 | **対応**⁴ |
| Carrier | Carrier | 64ビット（8バイト） | CARRIER_AC64 | **対応**⁵ |
| Hitachi | Hitachi AC | 28バイト | HITACHI_AC | **対応**⁶ |
| Haier | Haier AC | 9バイト | HAIER_AC | **対応**⁷ |

¹ Sharp（13バイト、A907モデル）は Samsung 同様、通常の IRremoteESP8266 ＋ HeatpumpIR ではなく IRremoteESP8266 双方向ペア（`sharp_irremoteesp8266_tx` / `_rx` — エンコードとデコードをそれぞれ実機上で独立スタックに対して検証）で検証する。HeatpumpIR に Sharp サポートが無いため。

² Samsung は通常の IRremoteESP8266 ＋ HeatpumpIR ではなく、IRremoteESP8266 の双方向ペア（`samsung_irremoteesp8266_tx` / `_rx` — エンコード・デコードを各々独立スタックで実機確認）で検証する: HeatpumpIR の Samsung クラスは旧 AQV（21バイト）と FJM（section2チェックサムが別）変種の実装で、いずれも現行の14バイト SAMSUNG_AC と一致しないため。

³ Kelvinator（16バイト、標準）は Samsung/Sharp 同様、通常の IRremoteESP8266 ＋ HeatpumpIR ではなく IRremoteESP8266 双方向ペア（`kelvinator_irremoteesp8266_tx` / `_rx` — エンコードとデコードをそれぞれ実機上で独立スタックに対して検証）で検証する。HeatpumpIR に Kelvinator サポートが無いため。加えて2ブロックのフレーミング（ブロック毎の B010 コマンドフッタ＋ギャップ）・ブロックチェックサムを host `codec_smoke` で検証し、実機の PulseKit 自己往復（`hardware/protocol_matrix_ac`）も通っている。

⁴ Midea（48ビット / 6バイト、標準）は Samsung/Sharp/Kelvinator 同様、通常の IRremoteESP8266 ＋ HeatpumpIR ではなく IRremoteESP8266 双方向ペア（`midea_irremoteesp8266_tx` / `_rx` — エンコードとデコードをそれぞれ実機上で独立スタックに対して検証）で検証する。HeatpumpIR に Midea サポートが無いため。加えて二重送信フレーミング（48bit のデータに続けてビット反転コピー）・チェックサムを host `codec_smoke` で検証し、実機の PulseKit 自己往復（`hardware/protocol_matrix_ac`）も通っている。

⁵ Carrier（64ビット / 8バイト、CARRIER_AC64）は Samsung/Sharp/Kelvinator/Midea 同様、通常の IRremoteESP8266 ＋ HeatpumpIR ではなく IRremoteESP8266 双方向ペア（`carrier_irremoteesp8266_tx` / `_rx` — エンコードとデコードをそれぞれ実機上で独立スタックに対して検証）で検証する: HeatpumpIR には Carrier クラスがあるが、CARRIER_AC64 ではなく別の NQV（9バイト）/ MCA（6バイト）変種の実装のため。加えて単一フレームの pulse-distance フレーミング・4bit ニブル総和チェックサムを host `codec_smoke` で検証し、実機の PulseKit 自己往復（`hardware/protocol_matrix_ac`）も通っている。

⁶ Hitachi（28バイト、HITACHI_AC）は Samsung/Sharp/Kelvinator/Midea/Carrier 同様、通常の IRremoteESP8266 ＋ HeatpumpIR ではなく IRremoteESP8266 双方向ペア（`hitachi_irremoteesp8266_tx` / `_rx` — エンコードとデコードをそれぞれ実機上で独立スタックに対して検証）で検証する。HeatpumpIR の Hitachi クラスは別（非28バイト）変種の実装のため。加えてバイト毎ビット反転のフィールドエンコード・総和ベースのチェックサムを host `codec_smoke` で検証し、実機の PulseKit 自己往復（`hardware/protocol_matrix_ac`）も通っている。他の Hitachi サイズ（13 / 27 / 33 / 37 / 43 / 53バイト）は別フレーム型として予約。

⁷ Haier（9バイト、HAIER_AC）は Samsung/Sharp/Kelvinator/Midea/Carrier/Hitachi 同様、通常の IRremoteESP8266 ＋ HeatpumpIR の組み合わせではなく IRremoteESP8266 双方向ペア（`haier_irremoteesp8266_tx` / `_rx` — エンコード・デコードそれぞれを独立スタックに対して実機検証）で検証している。HeatpumpIR に Haier サポートが無いため。ダブルヘッダのフレーミング（3000/4300 メインヘッダの前に 3000/3000 プリヘッダ）・コマンド指向の電源モデル・総和チェックサムは host `codec_smoke` でも検証済みで、実機で PulseKit 自己往復（`hardware/protocol_matrix_ac`）も通過。これは単一リファレンスの9バイト版で、YRW02（14バイト）・AC160（20バイト）・AC176（22バイト）は別フレーム型として予約。

**未着手の候補ベンダ。** 第2の独立リファレンス（IRremoteESP8266 *と* HeatpumpIR の両方）を持つ byte-state 候補は残っていない。さらにカバレッジを広げたい場合の単一リファレンス（IRremoteESP8266のみ）byte-state 候補: Haier の大型版（YRW02 14バイト・AC160 20バイト・AC176 22バイト）、TCL112（14バイト）、Electra（13バイト）。ビットペア方式（Coolix 24-bit、LG 28-bit など）は多バイトの byte-state ではなく別コード経路が必要なため、本レイヤの対象外。対象モデルは承認時に決定し、ここで固定はしない。

対応フォーマットのベンダ別構造:

- `Panasonic` — Kaseikyo/AEHA系: 2つのpulse-distanceフレーム（8バイト署名 + 19バイト状態）、LSBファースト、2フレーム目の総和チェックサム。`Model::JKE`（テンプレートは IRremoteESP8266 の既定known-good stateとバイト完全一致）、`DKE`/`NKE`/`LKE`/`RKR` に対応 — power/mode/temperature/fan のフィールドマップは共通で、固定マーカーバイトだけが異なる（`fromRaw` がモデル判定、`toRaw` が刻む）。各モデルを IRremoteESP8266 に対して実機検証済み。`CKP` は予約（トグル電源＋quiet/powerfulのビット位置が別）でエンコードは `false`。
- `Gree` — 8バイトの状態を2ブロックのpulse-distanceで送信。1ブロック目はバイト0〜3に固定3bitフッタを付け、2ブロック目はバイト4〜7をヘッダ無しで送る。Kelvinator系のブロックチェックサムがバイト7の上位ニブルに入る。実装モデルは YBOFB（`Model::YBOFB`、モデルビットは0）。`Model::YAW1F`/`YX1FSF` は将来用に予約。`Fan` は `AUTO`/`MIN_SPEED`/`MED_SPEED`/`MAX_SPEED`。`SwingV`/`SwingH` で上下・左右スイングを設定。
- `Mitsubishi` — 18バイトの「Mitsubishi AC」protocol（MSZ/霧ヶ峰系リモコン）: 固定5バイト署名を持つpulse-distanceフレーム1個を、長いギャップを挟んで2回送信。最終バイトは残りの総和チェックサム。このフォーマットは単一モデル（`Model` パラメータ無し）。他のMitsubishi波形フォーマット（136 / 112 / Heavy）は別の `Frame` 型になる。`Fan` は `AUTO`/`QUIET`/`LOW_SPEED`/`MED_SPEED`/`HIGH_SPEED`/`MAX_SPEED`。`Vane`（上下、`P1`..`P5`）と `WideVane`（左右）で気流方向を設定し、`temperatureC()`/`setTemperatureC()` は 0.5℃刻みを含む対称な `float` ペア。
- `Fujitsu` — 「Fujitsu AC」protocol（ARシリーズリモコン）、対象モデルは ARRAH2E。全設定は16バイトのpulse-distance「長」フレームで、固定バイト `14 63 00 10 10` で始まり、byte5 = `0xFE`（長フレームマーカー）、byte15 に補数チェックサム。電源OFFは7バイトの「短」フレーム `14 63 00 10 10 02 FD`（byte6 = `~`byte5）。各フレームは1回送信。電源は状態ビットでなくフレーム種別が表す（長=ON、短OFF=OFF）ので、`setPower(false)` は短フレームを送り、デコードしたOFFフレームの mode/temp/fan はベンダ的にdon't-careでテンプレート既定値を保持する。単一モデル（`Model` パラメータはまだ無し）。ARDB1 / ARJW2 / ARREB1E / ARRY4 / ARREW4E は長さ・マーカー・チェックサム補数・（ARREW4E は）温度エンコードが異なり、後でモデル分岐または `Frame` 型として追加する。`Fan` は `AUTO`/`HIGH_SPEED`/`MED_SPEED`/`LOW_SPEED`/`QUIET`。`Swing` は `OFF`/`VERTICAL`/`HORIZONTAL`/`BOTH`。
- `Daikin` — クラシックな「Daikin」/ ARC433 protocol（ARC433** / ARC466 リモコン、M Series / FTXM-M 機種）。35バイト状態を、先頭5bitの `00000` プリアンブルに続けて**3セクション**（8 + 8 + 19バイト）のpulse-distanceで送る。各セクションは独自のヘッダを持ち、セクション毎の総和チェックサム（byte7 / 15 / 34）を持つ。各セクションは固定署名 `11 DA 27` で始まる。単一のクラシックフォーマット（`Model` パラメータ無し）。他の Daikin サイズ（Daikin2 / 216 / 160 / 176 / 128 / 152 / 64 / 312）は別 `Frame` 型。`Mode` は `AUTO`/`DRY`/`COOL`/`HEAT`/`FAN`。`Fan` は `AUTO`/`QUIET`/`MIN_SPEED`/`LOW_SPEED`/`MED_SPEED`/`HIGH_SPEED`/`MAX_SPEED`。`setSwingVertical`/`setSwingHorizontal` で上下・左右の気流軸を切り替える。`temperatureC()`/`setTemperatureC()` は `float` ペア（byte22 に °C × 2 を格納）。位相整合キャリア必須——zeroスペースがbitマークと等しい（共に428us、§11.3）。
- `Toshiba` — 標準「Toshiba AC」protocol（WH-/RAS- リモコン、Carrier OEM機）。9バイトの pulse-distance フレームをフレームギャップを挟んで**2回**送る、**MSB-first**（唯一のMSB-first ACベンダ）、固定署名 `F2 0D` で始まる（byte1 = `~`byte0・byte3 = `~`byte2）。byte8 に XOR チェックサム（byte0–7）。電源は状態ビットでなく Mode フィールドが表す（`Mode == 7` = off）ので、`setPower(false)` は mode 7 を書き、`setPower(true)` は直前のモードを復元する。`Mode` は `AUTO`/`COOL`/`DRY`/`HEAT`/`FAN`。`Fan` は `AUTO`/`MIN_SPEED`/`LOW_SPEED`/`MED_SPEED`/`HIGH_SPEED`/`MAX_SPEED`（温度は整数のみ、17–30℃）。スイングは別の短メッセージ系（予約）で、標準フレームは保持しない。7バイト短・10バイト長メッセージは別 `Frame` 型。
- `Samsung` — 標準「Samsung AC」protocol（AR-/ARH- 系リモコン）。14バイト状態、**LSBファースト**。先頭一回限りのヘッダ（690us mark + 17844us space）の後、7バイトの2セクションを送る。各セクションは独自のセクションヘッダ（3086/8864us）と 2886us のセクションギャップを持つ。各セクションは popcount（ハミング重み）チェックサムをビット反転し、バイト1〜2の2ニブルに分割して格納する。固定ベンダ署名は無いため `fromRaw` は両セクションのチェックサム一致をゲートにする。電源は2bitフィールド2つ（byte6・byte13）: 両方 `0b11` = on、両方 `0b00` = off。`Mode` は `AUTO`/`COOL`/`DRY`/`FAN`/`HEAT`。`Fan` は `AUTO`/`LOW_SPEED`/`MED_SPEED`/`HIGH_SPEED`/`MAX_SPEED`（温度は整数のみ、16–30℃）。スイングと特殊ファン（Powerful/WindFree/Econo）はここでは設定不可。21バイト拡張（タイマ）メッセージは別 `Frame` 型。
- `Sharp` — 標準「Sharp AC」protocol。13バイトの単一 pulse-distance フレーム、**LSBファースト**、固定ヘッダ `AA 5A CF 10` で始まる。byte12 の上位ニブルにニブル畳み込みXORチェックサム。電源は4bitの `PowerSpecial` フィールド（byte5）: on=3 / off=2（単純なビットではない）。`Special` バイト（byte10）は実機がどのボタンを押したかを表すが、`toRaw` は常に「電源」値（0x00）を出すので完全な状態コマンドになる。`Mode` は `AUTO`/`HEAT`/`COOL`/`DRY`（Auto と Fan はワイヤコード `0b00` を共有するので Fan 単独は無し）。`Fan` は `AUTO`/`MIN_SPEED`/`MED_SPEED`/`HIGH_SPEED`/`MAX_SPEED`（非連続のワイヤコード 2/4/3/5/7、温度は整数 15–30℃）。Auto と Dry は温度を持たない（Temp=0）ので、温度はその場では don't-care。実装は既定の A907 モデル。A705 / A903（Heat を Fan に置換し別のファンコード、Model/Model2 ビットで区別）は予約。swing / ion / clean / timer は記載のみで setter 未作成。
- `Kelvinator` — 標準「Kelvinator」protocol（一部の Gree/Sharp ブランド機でも使用）。16バイト = 8バイト2ブロック、**LSBファースト**。各ブロックは ヘッダ＋32bit＋3bitコマンドフッタ（`B010`）＋約20msギャップ＋32bit＋約40msギャップ。byte8–10 は byte0–2 の複製、byte3 / byte11 は固定マーカー（`0x50` / `0x70`）、各ブロック末尾に4bitブロックチェックサム（byte7 / byte15 の上位ニブル、Gree方式: 10＋先頭4バイトの下位ニブル＋次3バイトの上位ニブル、mod 16）。`Mode` は `AUTO`/`COOL`/`DRY`/`FAN`/`HEAT`。`Fan` は `AUTO`/`MIN_SPEED`/`LOW_SPEED`/`MED_SPEED`/`HIGH_SPEED`/`MAX_SPEED`（低速側は byte0 の BasicFan にも反映、3で頭打ち）。温度は整数 16–30℃（Auto/Dry は 25℃ 固定）。単一フォーマットでモデル軸なし。上下/左右スイング・turbo・quiet・light・ion filter・X-Fan は記載のみで setter 未作成。
- `Midea` — 標準「Midea」protocol（OEM 多数: Pioneer・Comfee・Kaysun・Keystone・MrCool・Danby・Trotec・Lennox など）。48ビット / 6バイト、**MSBファースト**。1メッセージにつき2回送信: 48bit のデータ、続けて同じ48bit を全ビット**反転**したコピー（各コピーが 4480/4480 ヘッダ・560/1680 bit・5.6ms ギャップを持つ）。バイト順は送信順（byte0 = 固定 Header/Type バイト `0xA1`、byte5 = チェックサム）で、IRremoteESP8266 の `remote_state` union とは逆順。チェックサムは他5バイトをビット反転して総和し符号反転、さらにビット反転したもの。`fromRaw` は2コピー目が1コピー目の完全なビット反転であることと固定 Header フィールド（`0b10100`）で判定。`Mode` は `COOL`/`DRY`/`AUTO`/`HEAT`/`FAN`。`Fan` は `AUTO`/`LOW_SPEED`/`MED_SPEED`/`HIGH_SPEED`（4段のみ）。温度は整数 17–30℃で全モードで保持。摂氏のみ（華氏フラグは強制クリア）。単一フォーマットでモデル軸なし。sleep・オン/オフタイマー・センサ/follow-me・特殊トグルメッセージ（swing・econo・turbo・light・clean・8℃暖房・quiet）は記載のみで setter 未作成。
- `Carrier` — CARRIER_AC64 protocol（Carrier/Surrey 619EGX / 53NGK インバータ機のリモコン）。8バイト（64ビット）単一 pulse-distance フレーム、**LSBファースト**、1回送信。固定シグネチャ `0x84 0x55` で始まり、byte2 の下位ニブルに4bitチェックサム（それより上の全ニブル＝byte2 上位ニブル＋byte3–7 の総和）。`Mode` は `HEAT`/`COOL`/`FAN`（Auto/Dry なし）。`Fan` は `AUTO`/`LOW_SPEED`/`MED_SPEED`/`HIGH_SPEED`。温度は整数 16–30℃で全モードで保持。`SwingV` は設定可能。これは64ビット版で、他の Carrier 波形（AC / AC40 / AC84 / AC128）は別フレーム。sleep とオン/オフタイマーは記載のみで setter 未作成。
- `Hitachi` — 28バイト HITACHI_AC protocol（RAS-/RAK- 系リモコン）。単一 pulse-distance フレーム、**MSBファースト**、1回送信。固定9バイトフレーミングプレフィックス（`80 08 0C 02 FD 80 7F 88 48`）で始まり、最終バイトに総和ベースのチェックサム（62から他全バイトのビット反転値を引き、さらにビット反転）。特殊な点として各論理フィールドはバイト内で**ビット反転**して格納され、フィールドが結合している: Fan モードはセンチネル温度を持ち、Dry モードはファンを低速2段に制限し、モード変更でファンが再クランプされる（IRHitachiAc を正確に踏襲）。`Mode` は `AUTO`/`HEAT`/`COOL`/`DRY`/`FAN`。`Fan` は `AUTO`/`LOW_SPEED`/`MED_SPEED`/`HIGH_SPEED`（非連続のワイヤコード 1/2/3/5）。温度は整数 16–32℃。`SwingV`/`SwingH` は設定可能。これは28バイト版で、他の Hitachi サイズ（13 / 27 / 33 / 37 / 43 / 53バイト）は別フレーム。タイマーとコンフォート系フラグは setter 未作成。
- `Haier` — 9バイト HAIER_AC protocol（旧 HSU-/YR- 系リモコン）。単一 MSBファーストフレームで、特徴的な**ダブルヘッダ**（3000/3000 プリヘッダ＋3000/4300 メインヘッダ）を持ち、固定プレフィックス `0xA5` で始まり、末尾に総和チェックサム。**コマンド指向**で byte1 の下位ニブルが `Command` コード（押されたボタン）。永続的な電源ビットは無く、`setPower` は On/Off コマンドを書き、`power()` は「Off コマンドでない」を返す（Toshiba と同様）。`Mode` は `AUTO`/`COOL`/`DRY`/`HEAT`/`FAN`。`Fan` は `AUTO`/`LOW_SPEED`/`MED_SPEED`/`HIGH_SPEED`（ワイヤコードは反転: high=1, med=2, low=3）。温度は整数 16–30℃で全モードで保持。`SwingV`（`OFF`/`UP`/`DOWN`/`CYCLE`）は設定可能。これは9バイト版で、YRW02（14バイト）・AC160（20バイト）・AC176（22バイト）は別フレーム。タイマー・sleep・health は setter 未作成。

**Panasonic フィールドマップ（デコードされる論理フィールド）。** 各制御フィールドが27バイト状態のどこに入るか。ステータス凡例: ✅実装済（decode+encode）・🔜実装予定・🟡記載のみ（setter無し。RAW replayで再送）・⛔スコープ外（別Frame型）。

| フィールド | 位置 (byte/bit) | コード/値域 | ステータス |
|---|---|---|---|
| power | byte13 bit0 | on=1 / off=0 | ✅ |
| mode | byte13 上位ニブル | cool=3 / dry=2 / heat=4（auto=0 / fan=6） | ✅ |
| 温度(整数) | byte14 | `floor(℃) << 1`、16–30℃ | ✅ |
| 0.5℃ | byte22 bit7 | セットで +0.5℃ | ✅ |
| fan(風量) | byte16 上位ニブル | auto=A / 3..7（弱→最強） | ✅ |
| 風向(louver) | byte16 下位ニブル | 1–5=固定 / F=auto | ✅ |
| しずか/パワフル | byte21 bit5 / bit0 | しずか=0x20 / パワフル=0x01 | ✅（`Fan::QUIET`/`POWERFUL`） |
| タイマー入/切 | byte13 bit1,2 + byte18–20 | 11bit・分単位 | 🟡 |
| checksum | byte26 | frame2（byte8..25）の総和 mod256 | ✅ |

新規マップ分（0.5℃・風向・しずか/パワフル・タイマー）は実機1台（型番 ACXA75C15870、JKE系）からのリバースエンジニアリングで、power/mode/temperature/fan/checksum（IRremoteESP8266 でクロス検証済み）とは異なりモデル/ライブラリ横断の検証はまだ。しずか/パワフルは fan 速度と排他で、選ぶと fan ニブルが強制的に auto になり byte21 のフラグが立つ。よって 1つの `Fan` セレクタの値として持つ——`Fan::QUIET`/`POWERFUL` は fanニブル=auto＋byte21ビットに、速度 `MIN_SPEED`..`MAX_SPEED` はニブルにエンコードされる（タイマーの setter 非作成方針は [DESIGN.ja.md](DESIGN.ja.md)）。内部クリーン等の特殊ボタンは状態フレームと別系統の短縮コマンドフレームで送られる（⛔別Frame型・未対応）。RAW replay で再送は可能。

**Gree フィールドマップ（デコードされる論理フィールド）。** 各制御フィールドが8バイト状態（4バイト×2ブロック）のどこに入るか。ステータス凡例は上と同じ。

| フィールド | 位置 (byte/bit) | コード/値域 | ステータス |
|---|---|---|---|
| mode | byte0 bit0-2 | auto=0 / cool=1 / dry=2 / fan=3 / heat=4 | ✅ |
| power | byte0 bit3 | on=1 / off=0 | ✅ |
| fan(風量) | byte0 bit4-5 | auto=0 / 1–3（弱→最強） | ✅ |
| 上下スイング | byte0 bit6(auto) + byte4 bit0-3(位置) | auto / 位置 1–7, 9, 11 | ✅ |
| sleep | byte0 bit7 | on=1 | 🟡 |
| 温度 | byte1 bit0-3 | `℃ − 16`、16–30℃ | ✅ |
| タイマー | byte1 bit4-7 + byte2 bit0-3 | 有効＋10時間/30分/時間 | 🟡 |
| turbo | byte2 bit4 | on=1 | 🟡 |
| light | byte2 bit5 | on=1 | 🟡 |
| モデルマーカー | byte2 bit6 | YAW1F=1（YBOFB=0） | model param |
| xfan | byte2 bit7 | on=1 | 🟡 |
| 華氏 | byte3 bit3（＋bit2 で +0.5°F） | ℃/℉ 単位 | 🟡 |
| 左右スイング | byte4 bit4-6 | off / auto / 左…右 | ✅ |
| 表示温度ソース | byte5 bit0-1 | off / 設定 / 室内 / 室外 | 🟡 |
| iFeel | byte5 bit2 | on=1 | 🟡 |
| WiFi | byte5 bit6 | on=1 | 🟡 |
| econo | byte7 bit2 | on=1 | 🟡 |
| checksum | byte7 bit4-7 | Kelvinator ブロックのニブル総和 | ✅ |

byte3 の上位ニブル（`0b0101`）と byte5 bit3-5（`0b100`）はリモコンが常に持つ固定マーカー（フレームテンプレートに保持）。上下/左右スイングは両軸とも setter 実装済み（`SwingV`/`setSwingV`・`SwingH`/`setSwingH`）。`setSwingV` は選んだ値に応じて byte0 の SwingAuto ビットを整合させるので、auto モードと位置の食い違いは表現できない。コンフォート系トグル（sleep / turbo / light / xfan / econo / iFeel / WiFi）・タイマー・華氏モードは記載のみで setter は未作成 — キャプチャしたフレームを RAW replay で再送すれば再現できる。

**Mitsubishi AC フィールドマップ（デコードされる論理フィールド）。** 各制御フィールドが18バイト状態のどこに入るか。ステータス凡例は上と同じ。byte0–4 は固定署名。

| フィールド | 位置 (byte/bit) | コード/値域 | ステータス |
|---|---|---|---|
| power | byte5 bit5 | on=1 / off=0 | ✅ |
| mode | byte6 bit3-5 | heat=1 / dry=2 / cool=3 / auto=4 / fan=7 | ✅ |
| iSee センサ | byte6 bit6 | on=1 | 🟡 |
| 温度(整数) | byte7 bit0-3 | `℃ − 16`、16–31℃ | ✅ |
| 0.5℃ | byte7 bit4 | セットで +0.5℃ | ✅ |
| ワイドベーン(左右) | byte8 bit4-7 | 1–5（左→右）/ 6=ワイド / 8=auto | ✅ |
| fan(風量) | byte9 bit0-2 + bit7(auto) | 1–4（弱→最強）/ 5=しずか、bit7=auto | ✅ |
| ベーン(上下スイング) | byte9 bit3-5（＋bit6 有効） | auto=0 / 1–5（最上→最下）/ 7=スイング | ✅ |
| 時計 / 入切タイマー | byte10-13 | 現在/停止/開始時刻＋タイマーモードビット | 🟡 |
| ecocool | byte14 bit5 | on=1 | 🟡 |
| 直接/間接・i-save | byte15 | 気流方向 / i-save ビット | 🟡 |
| ナチュラルフロー・左ベーン | byte16 bit1 / bit3-5 | 左側のデュアルベーン | 🟡 |
| checksum | byte17 | byte0–16 の総和 mod256 | ✅ |

ベーン（上下スイング、`Vane` enum。位置は Panasonic のルーバーに倣い `P1`..`P5`——Arduino の `HIGH`/`LOW` マクロのため方向名は使えない）・ワイドベーン（左右、`WideVane`）・0.5℃（`temperatureC()`/`setTemperatureC()` を 0.5℃刻みの対称な `float` ペアに、`halfDegree()` は簡便な読み取り）はいずれも setter 実装済み。`setVane` は byte9 の「ベーン有効」ビットを立てる。`setMode` は byte8 を書き換えてワイドベーンを MIDDLE にリセットするので、先に mode、後からワイドベーンを設定する。時計/タイマーブロックとコンフォート/診断系ビット（iSee・ecocool・直接/間接・i-save・ナチュラルフロー・左ベーン）は記載のみで setter は未作成 — タイマーの setter 非作成方針（[DESIGN.ja.md](DESIGN.ja.md) §13）が同様に当てはまる。RAW replay で再現可能。

**Fujitsu AC フィールドマップ（デコードされる論理フィールド）。** 各制御フィールドが16バイト長フレーム状態（ARRAH2E）のどこに入るか。ステータス凡例は上と同じ。byte0–7 は固定/フレーミング: `14 63`（ヘッダ）、byte2=デバイスID、`10 10`、byte5=`0xFE` 長マーカー、byte6=残バイト長 `0x09`、byte7=protocol `0x30`。

| フィールド | 位置 (byte/bit) | コード/値域 | ステータス |
|---|---|---|---|
| power | フレーム種別 | 長フレーム=ON / 7バイト短 `…02 FD`=OFF | ✅ |
| 温度(整数) | byte8 bit2-7 | `(℃ − 16) × 4`（整数部は上位ニブル）、16–30℃ | ✅ |
| mode | byte9 bit0-2 | auto=0 / cool=1 / dry=2 / fan=3 / heat=4 | ✅ |
| fan(風量) | byte10 bit0-2 | auto=0 / high=1 / med=2 / low=3 / quiet=4 | ✅ |
| swing | byte10 bit4-5 | off=0 / 上下=1 / 左右=2 / 両方=3 | ✅ |
| 華氏 | byte8 bit1 | ℃/℉ 単位 | 🟡 |
| クリーン / 10℃暖房 | byte9 bit3 | on=1 | 🟡 |
| 入切タイマー | byte11-13 | 11bit・分単位＋有効ビット | 🟡 |
| フィルタ / 外部静音 | byte14 bit3, 7 | on=1 | 🟡 |
| checksum(長) | byte15 | `−(byte7…14 の総和)` mod256 | ✅ |
| checksum(短) | byte6 | `~`byte5（コマンドの反転） | ✅ |

電源は状態ビットでなく長/短フレームのセレクタ（長フレームでは byte8 の Power ビットは0のまま）: `setPower(true)` は全16バイト状態を、`setPower(false)` は短OFFコマンドを描画し、その mode/temp/fan はベンダ的にdon't-care（デコードしたOFFフレームは `power=off` を報告し残りはテンプレート既定値を保持）。`temperatureC()`/`setTemperatureC()` は 16–30℃ にクランプ。6bitのTempフィールドは `(℃ − 16) × 4` を格納するので整数度は byte8 の上位ニブルに乗る。華氏単位・クリーン/10℃暖房・タイマー・フィルタ/外部静音は記載のみで setter は未作成 — キャプチャしたフレームを RAW replay で再送すれば再現できる。実装モデルは ARRAH2E のみ。他のARシリーズは予約（対応一覧を参照）。

**Daikin classic（ARC433）フィールドマップ（デコードされる論理フィールド）。** 各制御フィールドが35バイト状態（3セクション: byte0–7 / 8–15 / 16–34、各セクションは `11 DA 27` 署名で始まる）のどこに入るか。ステータス凡例は上と同じ。

| フィールド | 位置 (byte/bit) | コード/値域 | ステータス |
|---|---|---|---|
| power | byte21 bit0 | on=1 / off=0 | ✅ |
| mode | byte21 bit4-6 | auto=0 / dry=2 / cool=3 / heat=4 / fan=6 | ✅ |
| 温度 | byte22 | `°C × 2`、10–32℃（0.5℃刻み） | ✅ |
| fan(風量) | byte24 bit4-7 | auto=A / quiet=B / 3–7（弱→最強） | ✅ |
| 上下スイング | byte24 bit0-3 | on=F / off=0 | ✅ |
| 左右スイング | byte25 bit0-3 | on=F / off=0 | ✅ |
| comfort | byte6 bit4 | on=1 | 🟡 |
| 入切タイマー | byte21 bit1-2 + byte26-28 | 有効＋12bit・分単位 | 🟡 |
| パワフル / しずか | byte29 bit0 / bit5 | on=1 | 🟡 |
| センサ / econo | byte32 bit1 / bit2 | on=1 | 🟡 |
| カビ防止(mold) | byte33 bit1 | on=1 | 🟡 |
| checksum（×3） | byte7 / 15 / 34 | セクション毎の総和 mod256 | ✅ |

状態は5bitの `00000` プリアンブル＋3セクションとして描画され、各セクションは独自の `3650/1623µs` ヘッダを持ち `~29ms` ギャップで終わる。`toRaw` はセクション署名と3つのチェックサムを書き直す。`temperatureC()`/`setTemperatureC()` は 10–32℃ にクランプし `°C × 2` を格納（0.5℃刻みが往復する）。comfort / タイマー / パワフル / しずか / センサ / econo / mold は記載のみで setter は未作成 — キャプチャしたフレームを RAW replay で再送すれば再現できる。実装済みは唯一このフォーマットで、他サイズは予約（対応一覧を参照）。

**Toshiba AC フィールドマップ（デコードされる論理フィールド）。** 各制御フィールドが9バイト状態のどこに入るか。ステータス凡例は上と同じ。byte0–4 は固定フレーミング: `F2 0D`（署名＋反転ペア）・`03 FC`（length/model＋反転ペア）・`01`（フラグ）。

| フィールド | 位置 (byte/bit) | コード/値域 | ステータス |
|---|---|---|---|
| power | byte6 bit0-2 (Mode) | on = mode≠7 / off = 7 | ✅ |
| mode | byte6 bit0-2 | auto=0 / cool=1 / dry=2 / heat=3 / fan=4 | ✅ |
| 温度 | byte5 bit4-7 | `°C − 17`、17–30℃（整数） | ✅ |
| fan(風量) | byte6 bit5-7 | auto=0 / 2–6（弱→最強） | ✅ |
| filter | byte7 bit4 | on=1 | 🟡 |
| swing | 短メッセージ系 | — | ⛔ 別フレーム |
| checksum | byte8 | byte0–7 の XOR | ✅ |

MSB-first。`toRaw` は固定フレーミング前置（署名＋反転ペア＋length/flags）を書き直し XOR チェックサムを再計算し、9バイトメッセージをフレームギャップを挟んで**2回**描画する（標準プロトコル準拠 — サードパーティのデコーダはフレーム間ギャップを終端スペースとして利用するため）。zeroスペースは公称490usではなく440usで送出する: 実機の受信機は mark/space 境界をずらして受信スペースを伸ばすため、490usだと復元後のzeroスペースが IRremoteESP8266 の厳しい既定上限（`490 − kMarkExcess(50) = 440`、×1.25 ≈ 551us）を超える。短めに出すことで余裕を確保しつつ、我々のデコーダ（許容30%）や通常バイアスの受信機でも復元できる。電源は Mode フィールドの off コード（7）なので `setPower(false)` は mode 7 を書く。filter ビットは setter 未作成、swing は別の短メッセージ（予約）。実装済みは唯一このフォーマットで、7バイト短・10バイト長は予約。

**Samsung AC フィールドマップ（デコードされる論理フィールド）。** 各制御フィールドが14バイト状態（7バイト×2セクション）のどこに入るか。ステータス凡例は上と同じ。

| フィールド | 位置（byte/bit） | コード / 範囲 | 状態 |
|---|---|---|---|
| power | byte6 bit4-5 ＋ byte13 bit4-5 | 両方 `0b11` = on / 両方 `0b00` = off | ✅ |
| mode | byte12 bit4-6 | auto=0 / cool=1 / dry=2 / fan=3 / heat=4 | ✅ |
| temperature | byte11 bit4-7 | `°C − 16`、16–30℃（整数） | ✅ |
| fan(風量) | byte12 bit1-3 | auto=0 / low=2 / med=4 / high=5 / max(turbo)=7 | ✅ |
| swing | byte9 bit4-6 | — | 🟡 |
| 特殊ファン（Powerful/WindFree/Econo） | byte10 bit1-3 | — | 🟡 |
| checksum（section 1） | byte1 上位ニブル ＋ byte2 下位ニブル | section バイト0–6 の popcount を反転 | ✅ |
| checksum（section 2） | byte8 上位ニブル ＋ byte9 下位ニブル | section バイト7–13 の popcount を反転 | ✅ |

LSB-first。`toRaw` は2つのセクションチェックサムを再計算し、一回限りの先頭ヘッダに続けて2セクション（各々独自ヘッダ付き）を実機同様に描画する。電源は2bitフィールド2つをまとめて書く。swing と特殊ファンは setter 未作成。実装済みは唯一このフォーマットで、21バイト拡張（タイマ）は予約。

**Sharp AC フィールドマップ（デコードされる論理フィールド）。** 各制御フィールドが13バイト状態のどこに入るか。ステータス凡例は上と同じ。byte0–3 は固定ヘッダ `AA 5A CF 10`。

| フィールド | 位置（byte/bit） | コード / 範囲 | 状態 |
|---|---|---|---|
| power | byte5 bit4-7（PowerSpecial） | on=3 / off=2 | ✅ |
| mode | byte6 bit0-1 | auto=0 / heat=1 / cool=2 / dry=3 | ✅ |
| temperature | byte4 bit0-3 | `°C − 15`、15–30℃（Cool/Heat、byte4上位ビットは固定 0xC0、Auto/Dry はバイト全体を0に） | ✅ |
| fan(風量) | byte6 bit4-6 | auto=2 / med=3 / min=4 / high=5 / max=7 | ✅ |
| model | byte4 bit4 ＋ byte11 bit4 | A907（実装）/ A705 / A903 | 🟡 |
| swing | byte8 bit0-2 | — | 🟡 |
| ion / clean / timer | byte6/7/11 | — | 🟡 |
| special（ボタン） | byte10 | power=0x00（出力） | ✅ |
| checksum | byte12 bit4-7 | byte0–11 ＋ byte12下位ニブルのニブル畳み込みXOR | ✅ |

LSB-first。`toRaw` は固定ヘッダを書き直し、Cool/Heat では byte4 上位ビットを 0xC0 にし（Auto/Dry は温度を持たないのでバイト全体を0に）、Special バイトを「電源」値にして、ニブルチェックサムを再計算する。電源は PowerSpecial フィールド（on=3 / off=2）。A705 / A903 モデル・swing・ion・clean・timer は setter 未作成。実装済みは A907 のみで、A705 / A903 は予約。

**Kelvinator フィールドマップ（デコードされる論理フィールド）。** 各制御フィールドが16バイト状態（8バイト2ブロック）のどこに入るか。ステータス凡例は上と同じ。byte3 = `0x50`・byte11 = `0x70` は固定マーカー、byte8–10 は byte0–2 の複製。

| フィールド | 位置（byte/bit） | コード / 範囲 | 状態 |
|---|---|---|---|
| power | byte0 bit3 | 0/1 | ✅ |
| mode | byte0 bit0-2 | auto=0 / cool=1 / dry=2 / fan=3 / heat=4 | ✅ |
| temperature | byte1 bit0-3 | `℃ − 16`、16–30℃（Auto/Dry は 25℃ 固定） | ✅ |
| fan | byte14 bit4-6（＋ byte0 bit4-5 BasicFan） | auto=0 / 1–5（BasicFan は3で頭打ち） | ✅ |
| 上下スイング | byte0 bit6（auto）＋ byte4 bit0-3（位置） | — | 🟡 |
| 左右スイング | byte4 bit4 | 0/1 | 🟡 |
| turbo / light / ion / X-Fan | byte2 bit4-7 | — | 🟡 |
| quiet | byte12 bit7 | — | 🟡 |
| checksum | byte7 bit4-7（ブロック1）/ byte15 bit4-7（ブロック2） | 4bit ブロック和 | ✅ |

LSB-first。`toRaw` は固定マーカーを強制し、byte0–2 を 8–10 に複製し、両ブロックチェックサムを再計算してから、2ブロック（ヘッダ・32bit・`B010` フッタ・約20msギャップ・32bit・約40msギャップ）を描画する。単一フォーマットでモデル軸なし。swing / turbo / quiet / light / ion / X-Fan は setter 未作成。

**Midea フィールドマップ（デコードされる論理フィールド）。** 各制御フィールドが6バイト状態のどこに入るか。ステータス凡例は上と同じ。バイト順は送信順（MSBファースト）: byte0 = 固定 Header/Type バイト、byte5 = チェックサム。byte3–4 は既定 `0xFF`（タイマー/センサ off）。

| フィールド | 位置（byte/bit） | コード / 範囲 | 状態 |
|---|---|---|---|
| header / type | byte0 | `0xA1`（Header `0b10100` ＋ command type `0b001`） | ✅ |
| power | byte1 bit7 | 0/1 | ✅ |
| mode | byte1 bit0-2 | cool=0 / dry=1 / auto=2 / heat=3 / fan=4 | ✅ |
| fan | byte1 bit3-4 | auto=0 / low=1 / med=2 / high=3 | ✅ |
| temperature | byte2 bit0-4 | `℃ − 17`、17–30℃（全モード） | ✅ |
| useFahrenheit | byte2 bit5 | 強制0（摂氏） | 🟡 |
| sleep | byte1 bit6 | 0/1 | 🟡 |
| オン/オフタイマー・センサ/follow-me | byte3-4 | — | 🟡 |
| swing / econo / turbo / light / clean / 8℃暖房 / quiet | 別トグルメッセージ | — | ⛔ |
| checksum | byte5 | ビット反転総和を符号反転しビット反転 | ✅ |

MSBファースト。`toRaw` は Header/Type バイトと摂氏を強制し、チェックサムを書き、2コピー（ヘッダ・48bit・トレーラ・5.6msギャップ）を描画する（2コピー目は全ビット反転）。`fromRaw` はその反転2コピー目の一致を要求する。単一フォーマットでモデル軸なし。sleep・タイマー・センサ/follow-me・トグルメッセージは setter 未作成。

**Carrier フィールドマップ（デコードされる論理フィールド）。** 各制御フィールドが8バイト状態のどこに入るか。ステータス凡例は上と同じ。byte0–1 は固定シグネチャ `0x84 0x55`（チェックサム対象外）、byte5 は未使用。

| フィールド | 位置（byte/bit） | コード / 範囲 | 状態 |
|---|---|---|---|
| signature | byte0-1 | 固定 `0x84 0x55` | ✅ |
| checksum | byte2 bit0-3 | bit20-63 の4bitニブル総和 | ✅ |
| mode | byte2 bit4-5 | heat=1 / cool=2 / fan=3 | ✅ |
| fan | byte2 bit6-7 | auto=0 / low=1 / med=2 / high=3 | ✅ |
| temperature | byte3 bit0-3 | `℃ − 16`、16–30℃（全モード） | ✅ |
| 上下スイング | byte3 bit5 | 0/1 | ✅ |
| power | byte4 bit4 | 0/1 | ✅ |
| オン/オフタイマー | byte4 bit5-6（有効化）＋ byte6-7 | — | 🟡 |
| sleep | byte4 bit7 | 0/1 | 🟡 |

LSBファースト。`toRaw` はシグネチャバイトを強制し、未使用バイトをクリアし、ニブルチェックサムを再計算してから、8バイトフレームを1回描画する（CARRIER_AC64 は1回送信）。単一フォーマットでモデル軸なし。sleep とタイマーは setter 未作成。

**Hitachi フィールドマップ（デコードされる論理フィールド）。** 各制御フィールドが28バイト状態のどこに入るか。ステータス凡例は上と同じ。各フィールドバイトはビット反転して格納。byte0–8 は固定フレーミングプレフィックス、byte9 は最低温度フラグ（`0x10`、16℃で `0x90`）、byte24 は固定 `0x80`。

| フィールド | 位置（byte/bit） | コード / 範囲 | 状態 |
|---|---|---|---|
| prefix | byte0-8 | 固定 `80 08 0C 02 FD 80 7F 88 48` | ✅ |
| mode | byte10（ビット反転） | auto=2 / heat=3 / cool=4 / dry=5 / fan=12 | ✅ |
| temperature | byte11（ビット反転 `℃ << 1`） | 16–32℃（Fan モードはセンチネル） | ✅ |
| fan | byte13（ビット反転） | auto=1 / low=2 / med=3 / high=5 | ✅ |
| 上下スイング | byte14 bit7 | 0/1 | ✅ |
| 左右スイング | byte15 bit7 | 0/1 | ✅ |
| power | byte17 bit0 | 0/1 | ✅ |
| タイマー / コンフォートフラグ | 各所 | — | 🟡 |
| checksum | byte27 | 62 − Σ ビット反転バイト、ビット反転 | ✅ |

MSBファースト。`toRaw` は固定プレフィックスと byte24 を強制し、チェックサムを再計算してから、28バイトフレームを1回描画する（1回送信）。setter は IRHitachiAc の結合（Fan モードのセンチネル温度・Dry モードのファンクランプ・モード変更時のファン再クランプ）を踏襲する。単一フォーマットでモデル軸なし。タイマーとコンフォートフラグは setter 未作成。

**Haier フィールドマップ（デコードされる論理フィールド）。** 各制御フィールドが9バイト状態のどこに入るか。ステータス凡例は上と同じ。byte0 は固定プレフィックス `0xA5`、byte2 bit5 は定数1、タイマーバイトは IRHaierAC のリセット既定値。

| フィールド | 位置（byte/bit） | コード / 範囲 | 状態 |
|---|---|---|---|
| prefix | byte0 | 固定 `0xA5` | ✅ |
| command / power | byte1 bit0-3 | On=1 / Off=0（power = Off以外） | ✅ |
| temperature | byte1 bit4-7 | `℃ − 16`、16–30℃（全モード） | ✅ |
| 上下スイング | byte2 bit6-7 | off=0 / up=1 / down=2 / cycle=3 | ✅ |
| fan | byte5 bit6-7 | 反転: auto=0 / high=1 / med=2 / low=3 | ✅ |
| mode | byte6 bit5-7 | auto=0 / cool=1 / dry=2 / heat=3 / fan=4 | ✅ |
| タイマー / sleep / health | byte2-7 | — | 🟡 |
| checksum | byte8 | byte0-7 の総和 | ✅ |

MSBファースト。`toRaw` はプレフィックスと定数ビットを強制し、総和チェックサムを再計算してから、ダブルヘッダ＋72bit＋トレーラを1回描画する（1回送信）。電源は On/Off コマンドなので、状態を組み立てる際は `setPower` を最後に呼ぶ。単一フォーマットでモデル軸なし。タイマー・sleep・health は setter 未作成。

AC型は送信APIではありません。送信は常に `IRSender::send()` が担当します。

### 11.3 長尺フレームのキャリア

ACフレームは長く、確実に届くキャリアはベンダのタイミング余裕に依存します。`setPhaseAlignedCarrier` を呼ばないときのライブラリのキャリアは位相整合（§6.5）です。

2つのキャリアは精度とサイズのトレードオフです。**位相整合**キャリアは各マークをキャリア整数周期ちょうどのシンボルで描画するため、±1周期の揺れがありません。ただし複数フレームのバーストは数千シンボルに膨らみ（1送信あたり約17KBを一時確保）、シンボル列が大きいぶん重い割り込み負荷下ではリフィルのアンダーランの危険性が増します。**ハードウェア**（自走）キャリアは必要シンボル数がはるかに少ない一方、マークごとに位相がリセットされず、各マークのエッジが最大キャリア1周期（38kHzで約26us）遅れ得ます。

この揺れは、タイミング余裕の狭いベンダで効いてきます。

- **Panasonic** はどちらのキャリアでも問題ありません（実機治具で両方とも全フレーム到達）。ハードウェアキャリアで十分かつ省メモリです。
- **Gree** は位相整合キャリアが必須です。zeroスペース（540us）がbitマーク（620us）より短いため、ハードウェアキャリアのマーク揺れでスペースが許容範囲を外れ、受信側がフレームの約半数を棄却します（実測: 位相整合 50/50 vs ハードウェア 約55%）。
- **Mitsubishi** も同じタイミング余裕の狭い例（zeroスペース420us < bitマーク450us）で、同様に位相整合キャリアを使います。
- **Fujitsu** も同じタイミング余裕の狭い例（zeroスペース390us < bitマーク448us）で、既定で位相整合キャリアを使います。`fujitsu_*` compat studies は実機での到達率を確認するためのものです。
- **Daikin** は最も極端な例で、zeroスペースがbitマークと等しい（共に428us）ため揺れの余裕がゼロで、位相整合キャリアが必須です。3セクションのバーストも長く、位相整合のシンボル数は全ベンダ中最大になります。
- **Toshiba** は zeroスペース（送出値440us — フレーミング注記参照）が bitマーク（580us）より短い、Fujitsu と同じ余裕の狭い例なので、既定で位相整合キャリアを使います。
- **Samsung** は zeroスペース（436us）が bitマーク（586us）より短いので、既定で位相整合キャリアを使います。セクションデコーダが mark-excess を使わないため、標準の436us zeroスペースは Toshiba で必要だった短縮送出なしでサードパーティの窓に収まります。

推奨: ACでは位相整合キャリアが安全な既定で、`setPhaseAlignedCarrier` を呼ばなければこれになります。ハードウェアキャリア（`setPhaseAlignedCarrier(false)`）は、Panasonic のようなタイミング余裕の広いベンダでの省メモリ最適化としてのみ使ってください。キャリアはバイト整合性ではなく到達率に影響します——受信できたフレームは常にバイト正確（チェックサム検証済み）で、位相整合キャリアにサイズ上限はありません（15bitフィールドを超える時間は複数シンボルに分割）。
