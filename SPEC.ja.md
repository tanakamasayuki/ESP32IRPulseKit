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
- `bits` のビット順は `IRProtocolSpec::lsb_first` に従います。

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
  PANASONIC = 3,
  SONY = 4,
  RC5 = 5,
  RC6 = 6,
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
- `name`: 表示用の固定長コピー文字列です。最大15文字 + 終端NULです。`addProtocol()` は `IRProtocolSpec` を値としてコピーするため、外部文字列の寿命管理に依存しません。
- 固定長protocolでは `bit_length` を使います。
- 可変長protocolでは `min_bit_length..max_bit_length` を使います。`0` の場合は `bit_length` を下限/上限として扱います。
- `default_repeat_count` は `repeat_count < 0` の送信で使う既定の追加送信回数です。`0` は1回だけ送信、`2` は合計3回送信です。
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
- `DECODE_SKIPPED`: RAW-only設定などでdecodeしなかったことを示します。
- `RAW_TRUNCATED`: RAWが内部上限を超えて切り詰められたことを示します。
- `RMT_OVERFLOW`: RMT受信でoverflowが発生した可能性を示します。

## 3. 内蔵Protocol

内蔵protocol specは `esp32irpk::specs` にあります。

```cpp
esp32irpk::specs::NEC
esp32irpk::specs::AEHA
esp32irpk::specs::PANASONIC40
esp32irpk::specs::PANASONIC48
esp32irpk::specs::SONY12
esp32irpk::specs::SONY15
esp32irpk::specs::SONY20
esp32irpk::specs::SAMSUNG32
esp32irpk::specs::SAMSUNG36
esp32irpk::specs::JVC24
esp32irpk::specs::JVC32
esp32irpk::specs::RC5
esp32irpk::specs::RC6_M0_16
esp32irpk::specs::RC6_M6_32
```

`IRProtocolID` は波形の近さだけでなく、`IRDecodedBits.bits` の論理解釈が互換かどうかで分けます。たとえばSamsung 32bitと36bitは別IDです。

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
- RMT TX出力はIR受信モジュール向けに38kHz carrierで変調します。carrier周波数の公開設定APIは現時点では持ちません。

### 6.2 BITS送信

`send(decoded)` は `IRDecodedBits` をRAWへencodeして送信します。

- `begin()` 前は `false` を返します。
- `decoded.protocol_id` に対応するprotocol未登録なら `false` を返します。
- 固定長protocolでは `decoded.bit_length == spec.bit_length` が必要です。
- 可変長protocolでは `decoded.bit_length` が `min_bit_length..max_bit_length` の範囲内である必要があります。
- repeatフレームは `spec.has_repeat == true` のprotocolでのみ送信できます。
- `repeat_count < 0` の場合は `spec.default_repeat_count` を使います。
- `repeat_count >= 0` の場合は呼び出し値を使います。

### 6.3 encode

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

## 8. BITS helper

よく使うFrame生成は `esp32irpk::bits` に小さなhelperを置くことがあります。helperは `IRDecodedBits` を返します。

```cpp
esp32irpk::IRDecodedBits bits = esp32irpk::bits::nec(0x00ff, 0x34);
tx.send(bits);
```

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
