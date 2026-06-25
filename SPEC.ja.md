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
  decode/encodeモデルには収まらない。汎用デコーダではなく別レイヤで扱う（§11参照）

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
- キャプチャしたRAWを `IRSender::send(const IRRawTickView&)` で再送する。長いフレームは `setPhaseAlignedCarrier(false)`（フリーランのハードウェアキャリア）にしてRMTシンボル数を抑える。

### 11.2 デコード＆エンコード（ベンダ別）

意味あるフィールドへのデコードとフレーム再生成はベンダごとに扱います。各ベンダは自分の名前空間 `esp32irpk::ac::<Vendor>` に**同じ構造**（`Mode` / `Fan` / `Frame`）を持ち、RAW tick とバイト構造の論理状態を変換します。汎用の `frames::*` の `fromBits`/`toBits` パターンを踏襲しつつ、RAWベース・バイト幅にしたものです。

`Mode`・`Fan` は単一の共通enumではなく **ベンダ別enum** です。各ベンダが実際に対応する値だけを持つため、非対応の設定は名前を付けることすらできません。共通メンバは命名規約（`AUTO`/`COOL`/`HEAT`/`DRY`/`FAN`、対応する場合のfan段）を揃え、どのベンダも同じ読み口になります。名前の**構造**は全ベンダ共通（`ac::<Vendor>::Mode::COOL`）で、**メンバ集合**がベンダ固有です。

```cpp
namespace esp32irpk::ac {

enum class AcVendor : uint16_t {
  UNKNOWN = 0,
  PANASONIC = 1,
  // ベンダは順次追加
};

namespace Panasonic {

// ベンダ別: Panasonicが対応する値だけ。共通メンバは命名規約に従う。
// このenumはPanasonicが持たない値を許さない。
enum class Mode : uint8_t { AUTO = 0, COOL, HEAT, DRY, FAN };
// Arduinoが LOW/HIGH をマクロ定義するため、fan段は `_SPEED` サフィックスを使う。
enum class Fan  : uint8_t { AUTO = 0, QUIET, LOW_SPEED, MED_SPEED, HIGH_SPEED, POWERFUL };

struct Frame {
  static constexpr size_t kMaxTicks = /* ベンダフレームの上限 */;

  uint8_t bytes[kBytes] = {}; // 復号した生の状態（中間形式）
  uint16_t byte_length = 0;
  bool checksum_ok = false;

  // `bytes` 上の論理アクセサ
  bool power() const;          void setPower(bool on);
  Mode mode() const;           void setMode(Mode m);
  uint8_t temperatureC() const; void setTemperatureC(uint8_t c);
  Fan fan() const;             void setFan(Fan f);

  static bool fromRaw(const esp32irpk::IRRawTickView& raw, Frame& out);
  bool toRaw(esp32irpk::IRRawTickBuffer& out) const;
};

} // namespace Panasonic

// 便利関数: 任意ベンダの frame をエンコードして1呼び出しで送信する。
template <class Frame>
bool send(esp32irpk::IRSender& tx, const Frame& frame);

}
```

- `Frame::fromRaw(raw, out)` はRAW tickを状態バイトへ復号し、ベンダのチェックサムを検証します。そのベンダのフレームでない場合は `false` を返し、チェックサムの可否は `out.checksum_ok` で別に報告します。
- `Frame::toRaw(out)` はチェックサムを再計算し、状態を caller提供の `IRRawTickBuffer` にRAW tickとして書き出します。結果は `IRSender::send(const IRRawTickView&)` で送信します。
- `ac::send(tx, frame)` は1呼び出し版です。`Frame::kMaxTicks` のスタックバッファへエンコードして送信し、エンコード/送信失敗時は `false` を返します。バッファを自分で管理したい場合は `toRaw` + `IRSender::send()` を使います。送信機のキャリアモード（長尺は `setPhaseAlignedCarrier(false)`）は従来どおり別に設定します。
- 中間形式はバイト配列です。power / mode / temperature / fan などの論理フィールドはそのバイト上のアクセサです。
- どのベンダもこの同じ構造を自分の `esp32irpk::ac::<Vendor>` 名前空間で提供します。enum→名前の文字列化（例 `Panasonic::toString(Mode)`）はベンダ単位で後から追加可能で、コア契約には必須ではありません。
- 最初の対応ベンダは Panasonic です。

AC型は送信APIではありません。送信は常に `IRSender::send()` が担当します。

### 11.3 長尺フレームのキャリア

ACフレームは長いです。`setPhaseAlignedCarrier` を呼ばないときのライブラリのキャリアは位相整合（§6.5）ですが、ACのサンプルでは `setPhaseAlignedCarrier(false)` で自走ハードウェアキャリアを明示的に選んでおり、長尺フレームではこれを推奨します。

位相整合キャリアは各マークをキャリア1周期ずつのシンボルで描画するため、複数フレームのACバーストは数千シンボルに膨らみます（1送信あたり約17KBを一時確保し、チャンネル経由でストリーム）。送信精度は高くなり得ます——各マークがキャリア整数周期ちょうどになり、自走キャリアの±1周期の揺れが無い——が、シンボル列が大きいぶん、重い割り込み負荷下ではリフィルのアンダーランの危険性が増します。ハードウェアキャリアは必要シンボル数がはるかに少なくこれを避けられるため、サンプルではACでこちらを設定しています。

これはハードな制限ではなくトレードオフです。位相整合キャリアでも長尺フレームは正しく送信でき（サイズ上限は無く、15bitフィールドを超える時間は複数シンボルに分割される）、実機治具では両キャリアとも同等にACフレームを到達させました。ACではどちらでも問題ありません。受信できたフレームは常にバイト正確（チェックサム検証済み）です。
