## 0. はじめに

### 0.1 本仕様書の目的
本仕様書は、ESP32 Arduino 向け IRリモコン送受信ライブラリ **ESP32IRPulseKit** の外部仕様（API・動作・設計方針）を定義する。  
実装の詳細（内部アルゴリズム、最適化、各プロトコルの厳密仕様）は本仕様書の対象外とする。

### 0.2 対象環境
- ESP32 Arduino ライブラリ
- ESP-IDF 5 系以降で使用可能な **最新RMTドライバ**を利用
- ビルド設定は C++20 指定初期化（designated initializers）を前提とする（例：`-std=gnu++20`）

### 0.3 ライブラリ名・ネームスペース規約
- ライブラリ名：**ESP32IRPulseKit**
- ネームスペース：**`esp32irpk`**
- プロジェクトURL：<https://github.com/tanakamasayuki/ESP32IRPulseKit>
- 本仕様書のサンプルコードは `using namespace` を使用せず、すべてフル修飾名で記述する。

### 0.4 非対象範囲
- 各IRプロトコル（NEC/SONY等）の個別仕様の規定
- デコード判定の順序・最適化などの内部実装手法
- 低レベルのRMTドライバの設定詳細

### 0.5 用語集（Glossary）
| 用語 | 意味 |
| --- | --- |
| us | マイクロ秒。外部APIで使う時間単位。Spec もこの単位で保持する。 |
| tick | 内部時間単位。1 tick = 10us。RAW tick 配列はこの単位で公開する。 |
| mark | IR LED が ON（搬送波が出る）区間の長さ。 |
| space | IR LED が OFF 区間の長さ。 |
| RAW | mark/space の連続列（エンコード前の信号表現）。本仕様では RAW と表記。 |
| IRRawTickView | RAW を tick 配列として参照する view。次回 `read()` まで有効。 |
| IRDecodedBits | プロトコル判定後の正規化ビット表現。本仕様では BITS（ビット列）と呼び、`protocol_id` / `bit_length` / `bits` を持つ。 |
| Frame（プロトコル別定義） | BITS（IRDecodedBits）をプロトコル固有フィールドにマッピングした表現。Appendix A 参照。 |
| Spec（IRProtocolSpec） | プロトコル定義（header、one/zero、gap、tolerance、bit order 等）。受信/送信で共通。 |
| scheme | ビット表現方式の大分類（SPACE_ENC 等）。共通処理分岐に使用。 |
| family | プロトコルの系統分類（NEC_LIKE 等）。共通ロジックの適用範囲に使用。 |
| decode（RAW→BITS） | RAW をプロトコル判定し IRDecodedBits に正規化する処理。 |
| encode（BITS→RAW） | IRDecodedBits から送信用 RAW を生成する処理。 |
| unpack（BITS→Frame） | BITS をプロトコル別 Frame 型に展開する処理。関数名 `fromBits()` を推奨。 |
| pack（Frame→BITS） | Frame を BITS に正規化する処理。関数名 `toBits()` を推奨。 |
| idle threshold | RMT が「無信号期間」としてフレームを切る閾値。 |
| frame_end_gap_us | Spec が定義するフレーム終端 gap（最小）。idle threshold 算出に使う。 |
| 候補（candidate） | decode が成立したプロトコル判定結果。score 付きで返る。 |
| score | 候補の信頼度。高いほど良い。減点方式で算出。 |
| RAW_TRUNCATED | 返却 RAW が最大長超過により切り詰められたことを示すフラグ。 |
| RMT_OVERFLOW | RMT 受信側でオーバーフローや取りこぼしが発生した可能性を示すフラグ。 |
| queue_overflow_count | 内部受信キュー溢れで drop oldest が発生した累積回数。 |

---

## 1. ライブラリ概要

### 1.1 目的
- ESP32 の RMT ハードウェアを用いた IR送受信
- 高精度（ソフトタイマ実装ではなくRMT利用）
- RAW受信 → RAW送信
- RAW受信 → decode（RAW→BITS） → send（学習リモコン用途）
- RAW受信 → decode（RAW→BITS） → Frame（プロトコル別型）まで `fromBits()` で unpack → アプリ側で利用
- Frame（プロトコル別型） → `toBits()` で pack（Frame→BITS） → send（プロトコル正規化から送信までを一貫提供）

### 1.2 特徴
- 受信はバックエンドで継続し、デコード処理中も受信を停止しない
- デコード結果は **BITS（`esp32irpk::IRDecodedBits`）**を返す（address/command 等の論理値は共通返却せず、必要なら `fromBits()` でプロトコル別 Frame 型へ unpack して得る）
- decode候補はスコア順で返却（上位N件）

### 1.3 設計方針（分離）
- 受信：`esp32irpk::IRReceiver`
- 送信：`esp32irpk::IRSender`
- HAL（RMT操作）と Codec（decode/encode）は分離
- プロトコルごとの再利用性のため `esp32irpk::IRProtocolSpec` を中心に設計する

### 1.4 時間単位の方針
- 内部：tick（10us）
- 外部API/Spec：us（マイクロ秒）に統一

---

## 2. 用語とデータ表現（共通）

### 2.1 tick と us
- 内部管理：`1 tick = 10us`
- 外部に出る数値：us単位
- RAWは tick 配列として公開する（内部配列と整合性が高く、メモリ効率がよい）

### 2.2 mark/space配列の前提
- RAWデータは mark から開始する
- mark/spaceが交互に並ぶことを前提とする

### 2.3 `esp32irpk::IRPulseUs`
mark/space をマイクロ秒単位で保持する単純なペア。
```cpp
namespace esp32irpk {
struct IRPulseUs {
  uint32_t mark_us = 0;  // mark の長さ（us）
  uint32_t space_us = 0; // space の長さ（us）
};
}
```

### 2.4 `esp32irpk::IRRawTickView`
受信RAWを tick 単位配列として参照するためのビュー（内部バッファ参照）。
```cpp
namespace esp32irpk {
struct IRRawTickView {
  const uint16_t* ticks = nullptr; // 1 tick = 10us, 内部バッファを指す
  size_t len = 0;                  // ticks 配列の要素数
};
}
```

### 2.5 `esp32irpk::IRDecodedBits`
プロトコル判定後の正規化 BITS（ビット列）表現を保持する。
```cpp
namespace esp32irpk {

enum class IRFrameType : uint8_t {
  NORMAL = 0,
  REPEAT = 1,
};

struct IRDecodedBits {
  IRProtocolID protocol_id;                  // 判定されたプロトコルID
  IRFrameType frame_type = IRFrameType::NORMAL; // NORMAL/REPEAT 等のフレーム種別
  uint16_t bit_length = 0;                   // BITS の長さ（0..64）
  uint64_t bits = 0;                         // LSB-first/MSB-first は Spec に従う

  bool isRepeat() const { return frame_type == IRFrameType::REPEAT; }
};

}
```

### 2.6 受信候補数のデフォルト
デコード候補を保持する最大件数のデフォルト値をライブラリ共通で定義する。
```cpp
namespace esp32irpk {
  inline constexpr size_t kDefaultMaxDecodeCandidates = 4;
}
```

### 2.7 データフローと変換用語
- 受信経路：RAW（`IRRawTickView`） → **decode（RAW→BITS）** → `IRDecodedBits` → 必要に応じて `fromBits()` で各プロトコルの Frame 型へ unpack。
- 送信経路：Frame 型 → `toBits()` で pack（Frame→BITS） → **encode（BITS→RAW）**（`IRSender::encodeFromBits()`）で RAW → RMT 送信。
- RAW / BITS / Frame と変換呼称は上記で統一する。

---

## 3. プロトコル定義：`esp32irpk::IRProtocolSpec`

### 3.1 目的
`esp32irpk::IRProtocolSpec` は、1つのIRプロトコル（または互換性のあるプロトコルバリエーション）を定義する設定構造体である。受信・送信両方で共通利用される。

### 3.2 scheme / family
- `scheme`：ビット表現方式の大分類（SPACE_ENC等）
- `family`：共通処理・共通判定ロジックの再利用単位（NEC_LIKE等）

### 3.3 IRProtocolSpec 定義
1つのプロトコルに必要な時間パラメータやビット長、リピート定義などをまとめた設定構造体。
```cpp
namespace esp32irpk {

struct IRProtocolSpec {
  IRProtocolID protocol_id;          // 一意なプロトコルID

  IRProtocolScheme scheme;           // ビット表現方式（例：SPACE_ENC）
  IRProtocolFamily family;           // プロトコル系統（例：NEC_LIKE）

  IRPulseUs header;                  // ヘッダ mark/space
  IRPulseUs one;                     // ビット1の mark/space
  IRPulseUs zero;                    // ビット0の mark/space
  IRPulseUs trailer;                 // トレーラ（終端） mark/space

  uint32_t frame_end_gap_us = 0;     // フレーム終端 gap（us）

  bool lsb_first = true;             // ビット順序（true: LSB first）

  uint16_t bit_length = 0;           // 既定のビット長

  bool has_repeat = false;           // REPEAT シーケンスがあるか
  IRPulseUs repeat_header;           // REPEAT 用ヘッダ
  uint32_t repeat_gap_us = 0;        // REPEAT 間の gap（us）

  uint16_t bit_tol_pct = 25;         // パルス許容誤差（%）
  uint16_t endgap_tol_pct = 30;      // 終端 gap の許容誤差（%）
};

} // namespace
```

### 3.4 C++20 指定初期化の例（参考）
```cpp
constexpr esp32irpk::IRProtocolSpec MyProto = {
  .protocol_id = esp32irpk::IRProtocolID::USER1,           // 一意なID
  .scheme      = esp32irpk::IRProtocolScheme::SPACE_ENC,   // ビット表現方式
  .family      = esp32irpk::IRProtocolFamily::NEC_LIKE,    // 系統

  .header      = { .mark_us = 4000, .space_us = 2000 },    // ヘッダ
  .one         = { .mark_us = 600,  .space_us = 1600 },    // 1
  .zero        = { .mark_us = 600,  .space_us = 600  },    // 0
  .trailer     = { .mark_us = 600,  .space_us = 0    },    // トレーラ

  .frame_end_gap_us = 30000,                               // 終端ギャップ
  .lsb_first        = true,                                // LSB first
  .bit_length       = 24,                                  // 既定ビット長
};
```

---

## 4. 内蔵プロトコル定義：`esp32irpk::specs`

### 4.1 目的
内蔵プロトコル定義（プリセット相当）を提供し、ユーザーは `addProtocol()` により登録して利用できる。

### 4.2 参照方法
```cpp
esp32irpk::specs::NEC
esp32irpk::specs::SONY12
esp32irpk::specs::SONY15
esp32irpk::specs::SONY20
esp32irpk::specs::SAMSUNG32
esp32irpk::specs::SAMSUNG36
```

### 4.3 bit_length バリエーションと protocol_id 分離方針（論理互換性）
本ライブラリにおける `protocol_id` は、単に波形の符号化方式（timing）が同一であることを示すものではなく、`IRDecodedBits.bits` の論理的な意味（Frame の pack/unpack 解釈）が互換であることを表す識別子として扱う。

- 波形的（scheme/family/timing）に類似していても、BITS のレイアウトや意味付けが異なり、同一の Frame（pack/unpack）で扱えない場合は別 `protocol_id` とする。

例：Samsung 32bit と 36bit は波形符号化方式が類似しているが、BITS の意味付けと pack/unpack 処理が互換ではないため、`SAMSUNG32` / `SAMSUNG36` のように別 `protocol_id` として提供する。

---

## 5. プロトコル登録と初期化（共通ルール）

### 5.1 begin前のみ有効
以下の設定は begin 前のみ有効とし、begin後は変更できない（呼び出しは失敗扱い）。

- `setPin()`
- `setInverted()`
- `addProtocol()`
- `clearProtocols()`
- `setDecodeCandidates()`
- `setIdleThresholdUs()`

### 5.2 Specの所有権
`addProtocol()` に渡された `IRProtocolSpec` は、ユーザーの Spec 寿命に依存しないよう内部でコピー保持される。  
なお、内蔵 Spec（`esp32irpk::specs::*`）は静的領域に存在するため、実装は内部参照保持による最適化を行ってもよい（外部仕様上はコピー保持と同等に扱う）。

### 5.3 begin時のデフォルト登録
- RX：decodeCandidates > 0 かつ登録が空の場合 → `esp32irpk::specs` を全登録
- RX：decodeCandidates == 0（RAW-only） → 自動登録しない
- TX：登録が空の場合 → `esp32irpk::specs` を全登録（ユーザーが追加したカスタム Spec はこれに加えて送信可能とする）

### 5.4 idle threshold 決定
- base：`setIdleThresholdUs()` 指定値、未指定は 30000us
- decode有効時：`max(base, max(spec.frame_end_gap_us))`
- RAW-only：baseのみ

---

## 6. 受信API：`esp32irpk::IRReceiver`

### 6.1 IRResultFlags / IRRxStats
受信結果の状態フラグと、ドロップ／オーバーフローなどの統計値。
```cpp
namespace esp32irpk {

enum class IRResultFlags : uint8_t {
  NONE           = 0,
  DECODE_SKIPPED = 1 << 0,
  RAW_TRUNCATED  = 1 << 1,
  RMT_OVERFLOW   = 1 << 2,
};

struct IRRxStats {
  uint32_t queue_overflow_count = 0; // 受信キュー溢れで drop した回数
  uint32_t rmt_overflow_count   = 0; // RMT ハード/ドライバの overflow 検知回数
  uint32_t raw_truncated_count  = 0; // RAW truncate を行った回数
};

}
```
- `DECODE_SKIPPED`：decodeCandidates==0 などの設定によりデコード処理を実行しなかった結果として候補が 0 件のときに付与される（内部エラーを示すものではない）。

### 6.2 IRReceiveResult
RAWビューとデコード候補一覧をまとめて返却する受信結果コンテナ。
```cpp
namespace esp32irpk {

struct IRDecodeCandidate {
  IRProtocolID protocol_id;   // 判定されたプロトコル
  int16_t score = 0;          // 減点後のスコア（大きいほど良い）
  IRDecodedBits decoded;      // 正規化された BITS
};

template <size_t MaxCandidates = esp32irpk::kDefaultMaxDecodeCandidates>
struct IRReceiveResult {
  IRRawTickView raw;                     // 最新RAWビュー
  IRResultFlags flags = IRResultFlags::NONE; // フレームに付随する状態フラグ

  uint8_t count = 0;                     // candidates の件数（型は uint8_t 固定）
  IRDecodeCandidate candidates[MaxCandidates]; // スコア順の候補

  const esp32irpk::IRDecodeCandidate* candidate() const;
  const esp32irpk::IRDecodedBits* bits() const;
};

}
```
- `candidate()` / `bits()` は `count == 0` の場合に `nullptr` を返す。`count` は `uint8_t` のため最大 255 件に制限される（`kDefaultMaxDecodeCandidates` の実用範囲では影響なし）。

### 6.3 クラス定義（概要）
```cpp
namespace esp32irpk {

template <size_t MaxCandidates = esp32irpk::kDefaultMaxDecodeCandidates>
class IRReceiver {
public:
  explicit IRReceiver(int gpio);
  IRReceiver(int gpio, bool inverted);

  bool setPin(int gpio);
  bool setInverted(bool inverted);

  bool setDecodeCandidates(uint8_t n);  // 0..MaxCandidates
  bool setIdleThresholdUs(uint32_t us);

  bool addProtocol(const IRProtocolSpec& spec);
  bool clearProtocols();

  bool begin();
  void end();

  bool read(IRReceiveResult<MaxCandidates>& out);

  bool decode(const IRRawTickView& raw,
              IRReceiveResult<MaxCandidates>& out) const; // optional

  IRRxStats stats() const;
  void resetStats(); // optional
};

}
```

### 6.4 readの戻り値仕様
- `true`：out.raw が有効（len>0）
- `false`：取得可能データなし（out内容は未定義）

### 6.5 受信キュー溢れ
- 実装は古いデータから破棄する（drop oldest）
- フレーム単位で対処できないため flags では返さない
- `IRRxStats.queue_overflow_count` に累積反映する

---

## 7. 送信API：`esp32irpk::IRSender`

### 7.1 クラス定義（概要）
```cpp
namespace esp32irpk {

struct IRRawTickBuffer {
  uint16_t* ticks = nullptr; // 出力先の tick バッファ（1 tick = 10us）
  size_t capacity = 0;       // ticks の最大要素数（呼び出し側が確保）
  size_t len = 0;            // エンコード結果の有効長（要素数）
};

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

  bool send(const esp32irpk::IRRawTickView& raw, uint8_t repeat_count = 0);
  bool send(const esp32irpk::IRRawTickView* raw, uint8_t repeat_count = 0); // raw==nullptr は送信せず false
  bool send(const IRDecodedBits& decoded, uint8_t repeat_count = 0);
  bool send(const IRDecodedBits* decoded, uint8_t repeat_count = 0); // decoded==nullptr は送信せず false

  bool encode(const IRDecodedBits& decoded,
              /*out*/ IRRawTickBuffer& out_raw); // optional

  bool sendNEC(uint16_t address, uint8_t command, bool repeat = false); // optional
};

}
```
- `IRRawTickBuffer` は呼び出し側がバッファと capacity を用意し、`encode()`/`send()` が `len` を設定する（`len <= capacity` になるよう実装する）。
- `send(const IRRawTickView*)` は `raw == nullptr` の場合は送信せず `false` を返す（参照版と同様、送信失敗時も `false` を返す）。
- `send(const IRDecodedBits*)` は `decoded == nullptr` の場合は送信せず `false` を返す（参照版と同様、送信失敗時も `false` を返す）。

---

## 8. デコード・スコアリング仕様（概要）
- decodeは登録済み全Specに対して実行する
- デコード判定は「早期除外（ハードNG）」と「スコアリング（減点方式）」の2段階で行う
  - 早期除外：ヘッダー不一致、必要パルス数不足、bit長不一致、mark/space列の破綻など明らかに成立しない候補を除外
  - スコアリング：成立候補について、各パルス（mark/space）の期待値に対する誤差を評価し、誤差の累積（平均誤差・最大誤差・分散等）に基づき減点方式でスコアを算出。極端に大きい誤差にはキャップ（上限）を設けてもよい
- この方式により送信ばらつきや受信ノイズに耐性を持たせつつ、近似プロトコル間の判定はスコア差で選択する
- end gap tolerance は減点対象（失格条件にしない）。終端GAPは連打/長押し・受信モジュール特性・環境光・搬送波ずれ・`idle threshold` によるクリップ等で揺らぎやすく、header/bit列が一致していれば同一プロトコルの可能性が高いため。減点強度の細部は実装責務
- 判定順序や重み付け、最適化は実装責務

---

## 9. 戻り値・エラー仕様
- 例外を使用しない
- 成否は bool 戻り値で表現
- begin前/後制約を破る呼び出しは false を返す
- read(false)時のoutは未定義
- send失敗理由：Spec未登録、bit_length不一致、repeat未対応、RMT TX失敗など

---

## 10. メモリ・バッファ仕様

### 10.1 IRRawTickView の寿命
`IRRawTickView.ticks` は内部バッファを参照する。  
次回 `read()` （成功・失敗を問わず）または `end()` 呼び出しまで有効とする。

### 10.2 RAW長上限・truncate
RAWが上限を超えた場合、切り詰め（truncate）または破棄する。  
切り詰め時は flags に `RAW_TRUNCATED` を立てる。

### 10.3 RMT overflow
RMT受信ハードウェア／ドライバオーバーフロー等が発生した場合、当該フレームの flags に `RMT_OVERFLOW` を設定する。

---

## 11. 実行コンテキスト・安全性
- readはポーリング型（コールバック提供しない）
- APIはISRから呼び出してはならない
- 同一インスタンスへの複数スレッド同時呼び出しは未定義（排他はユーザー責務）

---

## Appendix A. プロトコル別Frame型（参考）
本章は参考情報として、`esp32irpk::IRDecodedBits` をプロトコル固有の論理値（address/command等）へ変換する **Frame型** の例を示す。  
Frame型は以下の責務を持つ：

- `esp32irpk::IRDecodedBits` ⇄ プロトコル固有論理値 の相互変換
- 変換の公開APIは短い対称名 `fromBits()` / `toBits()` を推奨
- 内部処理として `unpackBits()` / `packBits()` を持ち、対応関係を明確化してよい

> 注意：本章は NEC を例にした参考であり、他プロトコル（SONY等）の詳細仕様は本仕様書では規定しない。

---

### A.1 NECFrame の例

以下は **NEC（32bit）** の代表的な論理解釈例である。  
本例は「典型的な NEC 互換（address 16bit + command 8bit + command_inv 8bit）」を想定する。

> `protocol_id` や bit配置は `esp32irpk::specs::NEC` の定義に合わせること。  
> （実装ではLSB/MSB順やビット配置はSpecに従う）

#### A.1.1 型定義（例）

```cpp
#pragma once
#include <stdint.h>

namespace esp32irpk {
  struct IRDecodedBits;     // forward decl
  enum class IRProtocolID : uint16_t;
  enum class IRFrameType : uint8_t;
}

namespace esp32irpk::frames {

struct NECFrame {
  // ---- Logical fields ----
  uint16_t address = 0;
  uint8_t command = 0;

  // ---- Repeat marker ----
  // 必須: frame_type==REPEAT。推奨: bit_length==0 かつ bits==all-ones を併用する。
  bool is_repeat = false;

  // ---- Construction / conversion ----
  static esp32irpk::frames::NECFrame fromBits(const esp32irpk::IRDecodedBits& in);
  esp32irpk::IRDecodedBits toBits() const;

private:
  // Internal helpers
  void unpackBits(uint64_t bits, uint16_t bit_length);
  uint64_t packBits() const;
};

} // namespace esp32irpk::frames
```

#### A.1.2 実装例（参考）

```cpp
#include <stdint.h>

namespace esp32irpk {
  struct IRDecodedBits {
    esp32irpk::IRProtocolID protocol_id;
    esp32irpk::IRFrameType frame_type;
    uint16_t bit_length;
    uint64_t bits;
  };

  enum class IRProtocolID : uint16_t {
    NEC = 1,
  };

  enum class IRFrameType : uint8_t {
    NORMAL = 0,
    REPEAT = 1,
  };
}

namespace esp32irpk::frames {

esp32irpk::frames::NECFrame NECFrame::fromBits(const esp32irpk::IRDecodedBits& in) {
  esp32irpk::frames::NECFrame f{};

  // protocol_id チェックは利用側方針により省略/実装
  // if (in.protocol_id != esp32irpk::IRProtocolID::NEC) { ... }

  // 必須: frame_type==REPEAT。推奨: bit_length==0 かつ bits==all-ones を併用。
  if (in.frame_type == esp32irpk::IRFrameType::REPEAT) {
    f.is_repeat = true;
    return f;
  }

  f.unpackBits(in.bits, in.bit_length);
  return f;
}

void NECFrame::unpackBits(uint64_t bits, uint16_t bit_length) {
  // 代表例：NEC 32bit（LSB-firstを想定）
  // bits[0..15] : address
  // bits[16..23]: command
  // bits[24..31]: command_inv

  (void)bit_length; // 実装では bit_length==32 を確認してよい

  uint16_t addr = static_cast<uint16_t>(bits & 0xFFFFULL);
  uint8_t cmd   = static_cast<uint8_t>((bits >> 16) & 0xFFULL);
  uint8_t inv   = static_cast<uint8_t>((bits >> 24) & 0xFFULL);

  // 簡易整合チェック（実装では decodeスコアに反映してもよい）
  // if (inv != static_cast<uint8_t>(~cmd)) { ... }

  this->address = addr;
  this->command = cmd;
}

uint64_t NECFrame::packBits() const {
  // repeat の場合は特殊値（all-ones）を返す方針（v1.0推奨）
  if (this->is_repeat) {
    return 0xFFFFFFFFFFFFFFFFULL;
  }

  uint64_t bits = 0;
  bits |= static_cast<uint64_t>(this->address);
  bits |= (static_cast<uint64_t>(this->command) << 16);
  bits |= (static_cast<uint64_t>(~this->command) << 24);
  return bits;
}

esp32irpk::IRDecodedBits NECFrame::toBits() const {
  esp32irpk::IRDecodedBits out{};
  out.protocol_id = esp32irpk::IRProtocolID::NEC;

  if (this->is_repeat) {
    out.frame_type  = esp32irpk::IRFrameType::REPEAT;
    out.bit_length  = 0;
    out.bits        = 0xFFFFFFFFFFFFFFFFULL;
    return out;
  }

  out.frame_type  = esp32irpk::IRFrameType::NORMAL;
  out.bit_length  = 32;
  out.bits        = this->packBits();
  return out;
}

} // namespace esp32irpk::frames
```

---

### A.2 利用例（bits ⇄ NECFrame ⇄ send）

#### A.2.1 受信結果（IRDecodedBits）から論理値へ

```cpp
esp32irpk::IRReceiveResult<4> r;
if (rx.read(r) && r.count > 0) {
  const esp32irpk::IRDecodedBits& b = r.candidates[0].decoded;

  esp32irpk::frames::NECFrame f = esp32irpk::frames::NECFrame::fromBits(b);
  if (!f.is_repeat) {
    // f.address / f.command を利用できる
  }
}
```

#### A.2.2 論理値から bits を生成して送信

```cpp
esp32irpk::frames::NECFrame f{};
f.address = 0x00FF;
f.command = 0x12;

esp32irpk::IRDecodedBits b = f.toBits();
tx.send(b, /*repeat_count=*/2); // 合計3回送信（実装がNEC repeat frameを使ってもよい）
```

---

### A.3 設計上の注意

- `NECFrame` の bit配置は実装の `IRProtocolSpec`（LSB/MSB順、フィールド割当）と一致させること
- repeat表現は **frame_type==REPEAT が必須**。可読性のため `bit_length==0` かつ `bits==all-ones` を併用することを推奨する（将来拡張で変更される可能性がある）
- `fromBits()` / `toBits()` は decode/encode の本体ではなく、論理値と `IRDecodedBits` の相互変換に徹する


---

## Appendix B. サンプル集（フル修飾）
本章は簡易サンプルであり、エラーハンドリングや詳細設定は省略する。  
すべて `using namespace` を使用せず、フル修飾名で記述する。

---

### B.1 RAW受信 → RAW送信（学習リモコン：波形そのまま再生）

```cpp
#include <ESP32IRPulseKit.h>

esp32irpk::IRReceiver    rx(4, true);   // RX GPIO=4, 通常は反転入力
esp32irpk::IRSender      tx(5);         // TX GPIO=5

void setup() {
  rx.setDecodeCandidates(0); // RAW-only
  rx.begin();
  tx.begin();
}

void loop() {
  esp32irpk::IRReceiveResult r;
  if (rx.read(r)) {
    // RAW tick列をそのまま送信
    tx.send(r.raw);
  }
}
```

---

### B.2 RAW受信 → BITS → 送信（best候補のBITSを送る）

```cpp
#include <ESP32IRPulseKit.h>

esp32irpk::IRReceiver   rx(4, true); // RX GPIO=4, 通常は反転入力
esp32irpk::IRSender     tx(5);       // TX GPIO=5

void setup() {
  rx.begin();
  tx.begin();
}

void loop() {
  esp32irpk::IRReceiveResult r;
  if (rx.read(r)) {
    // best候補のBITSを送信（候補なし/nullptrなら送信しない）
    tx.send(r.bits());
  }
}
```

---

### B.3 NEC Frame → BITS送信（論理値から生成して送る）

```cpp
#include <ESP32IRPulseKit.h>

esp32irpk::IRSender tx(5);

void setup() {
  tx.begin();
}

void loop() {
  // 例：NECアドレス0x00FF / コマンド0x12
  esp32irpk::frames::NECFrame f{};
  f.address = 0x00FF;
  f.command = 0x12;

  // 例：合計3回送信（repeat_count=2）
  tx.send(f.toBits(), 2);

  delay(1000);
}
```

---

### B.4 RAW受信 → 8件BITS → 確認（候補スコアを一覧表示）

```cpp
#include <ESP32IRPulseKit.h>

esp32irpk::IRReceiver<8> rx(4);   // 最大8候補
esp32irpk::IRSender      tx(5);

void setup() {
  Serial.begin(115200);
  rx.begin();
  tx.begin();
}

static void printBits(uint64_t v) {
  // 16進表示（64bit）
  char buf[32];
  snprintf(buf, sizeof(buf), "0x%08lx%08lx", (uint32_t)(v >> 32), (uint32_t)(v & 0xFFFFFFFF));
  Serial.print(buf);
}

void loop() {
  esp32irpk::IRReceiveResult<8> r;
  if (rx.read(r)) {
    Serial.println("---- IR received ----");
    Serial.print("raw.len(ticks)=");
    Serial.println((unsigned)r.raw.len);

    if (r.count == 0) {
      Serial.println("no decoded candidates");
      return;
    }

    for (uint8_t i = 0; i < r.count; ++i) {
      const esp32irpk::IRDecodeCandidate& c = r.candidates[i];
      const esp32irpk::IRDecodedBits& b = c.decoded;

      Serial.print("#"); Serial.print(i);
      Serial.print(" protocol_id="); Serial.print((unsigned)c.protocol_id);
      Serial.print(" score="); Serial.print((int)c.score);
      Serial.print(" bit_length="); Serial.print((unsigned)b.bit_length);
      Serial.print(" bits="); printBits(b.bits);
      Serial.println();
    }
  }
}
```
