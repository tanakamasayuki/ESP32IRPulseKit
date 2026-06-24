#include <ESP32IRPulseKit.h>

// ===========================================================================
// DRAFT — usability review. The RAW-capture path (`setMaxRxSymbols`, §11.1) is
//         implemented, but the Panasonic decode backend
//         (`esp32irpk::ac::Panasonic::Frame`, §11.2) is still a Step 2 skeleton
//         stub: `fromRaw` returns false, so the decoded comment shows
//         "no AC vendor matched" until Step 3. The decode accessors
//         (power/mode/temp/fan) are PROVISIONAL. This compiles, but is excluded
//         from the build tests until the backend is functional.
// 草案 — 使い勝手レビュー。RAWキャプチャ経路（`setMaxRxSymbols`、§11.1）は実装
//        済みだが、Panasonicデコードのバックエンド
//        （`esp32irpk::ac::Panasonic::Frame`、§11.2）はまだStep 2のスケルトン
//        スタブで `fromRaw` は false を返す（Step 3まではコメントは
//        "no AC vendor matched"）。デコードのアクセサ（power/mode/temp/fan）は
//        仮。コンパイルは通るが、機能するまでビルドテストからは除外している。
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
