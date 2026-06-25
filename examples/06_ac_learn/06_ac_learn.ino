#include <ESP32IRPulseKit.h>

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
//     decoded settings as a comment. Vendors are added incrementally. When none
//     match, the RAW snippet below still reproduces the frame.
// ja: デコードは受信時にここで行う: 各ACベンダを試し、デコードした設定をコメント
//     として出力する。ベンダは順次追加。どれにも当たらなくても下のRAWスニペット
//     でフレームは再現できる。

// en: helpers live in an anonymous namespace: the Arduino .ino preprocessor does
//     not auto-generate prototypes for namespaced functions, which is required
//     for the printAcFrame template (a generated prototype would drop the
//     `template` line and fail to compile).
// ja: ヘルパは無名名前空間に置く。Arduinoの.ino前処理は名前空間内の関数に自動
//     プロトタイプを生成しないため、テンプレート printAcFrame が壊れずに済む。
namespace
{
// en: print "power/mode/temp/fan/checksum" plus the full state in hex. The hex
//     line is handy for inspecting fields the named accessors do not expose yet
//     (e.g. half-degree or vendor flags). Shared by all vendors.
// ja: power/mode/temp/fan/checksum とデコード状態全体のhexを出力する。hex行は
//     名前付きアクセサが未公開のフィールド（0.5℃やベンダ固有フラグ等）の確認に
//     便利。全ベンダ共通。
template <class Frame>
void printAcFrame(const char *vendor, const Frame &f)
{
  Serial.print("// decoded: ");
  Serial.print(vendor);
  Serial.print(" AC  power=");
  Serial.print(f.power() ? "on" : "off");
  Serial.print(" mode=");
  Serial.print((unsigned)f.mode());
  Serial.print(" temp=");
  Serial.print((unsigned)f.temperatureC());
  Serial.print("C fan=");
  Serial.print((unsigned)f.fan());
  Serial.print("  checksum=");
  Serial.println(f.checksum_ok ? "ok" : "BAD");
  Serial.print("// bytes:");
  for (size_t i = 0; i < f.byte_length; ++i)
  {
    Serial.print(' ');
    if (f.bytes[i] < 0x10)
      Serial.print('0');
    Serial.print(f.bytes[i], HEX);
  }
  Serial.println();
}

static void printDecodedComment(const esp32irpk::IRRawTickView &raw)
{
  esp32irpk::ac::Panasonic::Frame pf;
  if (esp32irpk::ac::Panasonic::Frame::fromRaw(raw, pf))
  {
    printAcFrame("Panasonic", pf);
    return;
  }
  esp32irpk::ac::Gree::Frame gf;
  if (esp32irpk::ac::Gree::Frame::fromRaw(raw, gf))
  {
    printAcFrame("Gree", gf);
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
} // namespace

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
