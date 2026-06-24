#include <ESP32IRPulseKit.h>

// ===========================================================================
// DRAFT — usability review. The RAW-capture path (`setMaxRxSymbols`, §11.1) and
//         the Panasonic decode backend (`esp32irpk::ac::Panasonic`, §11.2) are
//         implemented and self-consistent, but the field map (power/mode/temp/
//         fan offsets and codes) is PROVISIONAL — calibrated against real
//         hardware / IRremoteESP8266 in Step 3c. Compiles; excluded from the
//         build tests until field-map verification.
// 草案 — 使い勝手レビュー。RAWキャプチャ経路（`setMaxRxSymbols`、§11.1）と
//        Panasonicデコードのバックエンド（`esp32irpk::ac::Panasonic`、§11.2）は
//        実装済み・内部整合だが、フィールドマップ（power/mode/temp/fan のバイト
//        位置・コード）は仮で、Step 3c で実機/IRremoteESP8266 と突き合わせて確定
//        する。コンパイルは通るが、フィールドマップ検証までビルドテストからは除外。
// ===========================================================================

// en: Learn an air-conditioner remote: capture it as RAW and print copy-paste
//     C++ to send it, plus a decoded summary (as comments) when the vendor is
//     recognized. RX only, so it runs on a single board (TX is usually a
//     separate device). The RAW path reproduces any AC frame verbatim even when
//     no vendor decoder matches. Adjust the GPIO number to your board wiring.
// ja: エアコンのリモコンを学習する例: RAWでキャプチャして再送用のC++コードを
//     貼り付け可能な形で出力し、ベンダが認識できればデコード結果も（コメントで）
//     出力します。受信のみなので1台で動きます（送信側は別デバイスが普通）。
//     ベンダデコーダに当たらなくてもRAW経路でそのまま再現できる。
//     GPIO番号はご利用環境の配線に合わせて変更してください。
esp32irpk::IRReceiver rx(32, true); // en: most receiver modules output inverted / ja: 多くの受信モジュールは出力反転

// en: Decode is done here at receive time: try each AC vendor and print the
//     decoded settings as a comment. Vendors are added incrementally; for now
//     only Panasonic. When none match, the RAW snippet below still reproduces
//     the frame.
// ja: デコードは受信時にここで行う: 各ACベンダを試し、デコードした設定をコメント
//     として出力する。ベンダは順次追加で、今はPanasonicのみ。どれにも当たらなくて
//     も下のRAWスニペットでフレームは再現できる。
static void printDecodedComment(const esp32irpk::IRRawTickView &raw)
{
  esp32irpk::ac::Panasonic::Frame f;
  if (esp32irpk::ac::Panasonic::Frame::fromRaw(raw, f))
  {
    Serial.print("// decoded: Panasonic AC  power=");
    Serial.print(f.power() ? "on" : "off");
    Serial.print(" mode=");
    Serial.print((unsigned)f.mode());
    Serial.print(" temp=");
    Serial.print((unsigned)f.temperatureC());
    Serial.print("C fan=");
    Serial.print((unsigned)f.fan());
    Serial.print("  checksum=");
    Serial.println(f.checksum_ok ? "ok" : "BAD");
    return;
  }
  Serial.println("// decoded: no AC vendor matched (raw replay still works)");
}

// en: Print the captured RAW as a ready-to-send tick array. AC frames are long,
//     so this array is large — that is expected.
// ja: キャプチャしたRAWを送信用のtick配列として出力します。ACフレームは長いので
//     配列は大きくなります（想定どおり）。
static void printRawSnippet(const esp32irpk::IRRawTickView &raw)
{
  Serial.println("// send code (raw replay):");
  Serial.println("// en: send with a sender configured as setPhaseAlignedCarrier(false)");
  Serial.println("// ja: setPhaseAlignedCarrier(false) にした送信機で送ること");
  Serial.print("const uint16_t ticks[] = {");
  for (size_t i = 0; i < raw.len; ++i)
  {
    if (i)
    {
      Serial.print(", ");
    }
    Serial.print((unsigned)raw.ticks[i]); // en: 1 tick = 10us / ja: 1 tick = 10us
  }
  Serial.println("};");
  Serial.print("tx.send({ticks, ");
  Serial.print((unsigned)raw.len);
  Serial.println("});");
}

void setup()
{
  Serial.begin(115200);

  // en: AC capture mode = RAW-only + large capacity + idle long enough to keep
  //     the whole multi-frame burst in one capture.
  // ja: AC受信モード = RAWのみ + 容量拡張 + バースト全体を1キャプチャに収める
  //     ための十分長いidle。
  rx.setDecodeCandidates(0);       // en: RAW-only, no decode / ja: RAWのみ、デコードなし
  rx.setMaxRxSymbols(1024);        // en: caps total capture length / ja: キャプチャ総長の上限
  // en: idle ends the capture after this much silence, and must exceed the
  //     frame's INTERNAL gap so a multi-frame AC burst is not split. Prefer a
  //     generous value. Ceiling is ~300ms: a single duration is a 15-bit field
  //     at the 10us RX tick (0x7FFF * 10us ~= 327ms). If one remote press prints
  //     "---- learned ----" more than once, the burst was split -> raise this.
  // ja: idleはこの長さの無信号でキャプチャを終了する。ACのフレーム内ギャップより
  //     大きくしないとバーストが分割される。大きめ推奨。上限は約300ms（単一
  //     durationは10us tickで15bit = 0x7FFF*10us ≈ 327ms）。1回の押下で
  //     "---- learned ----" が複数回出たら分割されているので値を上げる。
  rx.setIdleThresholdUs(100000);   // en: 100ms, well within the ~300ms ceiling / ja: 100ms、上限~300ms内
  rx.begin();
}

void loop()
{
  esp32irpk::IRReceiveResult<> r;
  if (!rx.read(r))
  {
    delay(1);
    return;
  }

  Serial.println("---- learned ----");
  Serial.print("raw.len(ticks)=");
  Serial.print((unsigned)r.raw.len);
  if (r.truncated())
  {
    Serial.print("  (TRUNCATED — increase setMaxRxSymbols)");
  }
  Serial.println();

  printDecodedComment(r.raw); // en: decoded summary as a comment / ja: デコード結果をコメント出力
  printRawSnippet(r.raw);
  Serial.println();
}
