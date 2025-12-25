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
  uint64_t bits = 0;                         // LSB-first/ MSB-first は Spec に従う

  bool isRepeat() const { return frame_type == IRFrameType::REPEAT; }
};

}
```

### 2.6 データフローと変換用語
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
`addProtocol()` は Spec を内部でコピー保持する。  
ユーザーは Spec を `constexpr/static` として保持する必要はない。

### 5.3 begin時のデフォルト登録
- RX：decodeCandidates > 0 かつ登録が空の場合 → `esp32irpk::specs` を全登録
- RX：decodeCandidates == 0（RAW-only） → 自動登録しない
- TX：登録が空の場合 → `esp32irpk::specs` を全登録

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

### 6.2 IRReceiveResult
RAWビューとデコード候補一覧をまとめて返却する受信結果コンテナ。
```cpp
namespace esp32irpk {

template <size_t MaxCandidates>
struct IRReceiveResult {
  IRRawTickView raw;                     // 最新RAWビュー
  IRResultFlags flags = IRResultFlags::NONE; // フレームに付随する状態フラグ

  uint8_t count = 0;                     // candidates の件数
  IRDecodeCandidate candidates[MaxCandidates]; // スコア順の候補
};

}
```

### 6.3 クラス定義（概要）
```cpp
namespace esp32irpk {

template <size_t MaxCandidates = 4>
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

  bool send(const IRDecodedBits& decoded);

  bool encodeFromBits(const IRDecodedBits& decoded,
                      /*out*/ IRRawTickBuffer& out_raw); // optional

  bool sendNEC(uint16_t address, uint8_t command, bool repeat=false); // optional
};

}
```

---

## 8. デコード・スコアリング仕様（概要）
- decodeは登録済み全Specに対して実行する
- 明らかに違うもの（ヘッダ不一致など）は候補として返さない
- 成立候補のみスコアを返す（減点方式）
- 判定順序や最適化は実装責務
- end gap tolerance は減点対象（失格条件にしない）

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
- `fromBits()`（unpack） / `toBits()`（pack） を推奨
- 具体例は参考情報として記載可能（NECなど）

---

## Appendix B. サンプル集（フル修飾）

### B.1 学習リモコン：受信→再送
```cpp
esp32irpk::IRReceiver<4> rx(4);
esp32irpk::IRSender tx(5);

rx.begin();
tx.begin();

esp32irpk::IRReceiveResult<4> r;
if (rx.read(r) && r.count > 0) {
  tx.send(r.candidates[0].decoded);
}
```
