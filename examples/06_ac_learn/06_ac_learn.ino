#include <ESP32IRPulseKit.h>
#include <IRDebug.h> // en: optional Serial-formatting helpers / ja: 任意のシリアル整形ヘルパー

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

// en: helpers live in an anonymous namespace and are defined before use, so the
//     Arduino .ino preprocessor does not auto-generate prototypes for them
//     (its generated prototypes can mishandle namespaced/overloaded helpers).
// ja: ヘルパは無名名前空間に置き使用前に定義する。こうすればArduinoの.ino前処理が
//     自動プロトタイプを生成する必要がなく（生成プロトタイプは名前空間内/多重定義の
//     ヘルパを壊すことがある）安全。
namespace
{
// en: AC RAW snippet: a phase-aligned-carrier reminder (AC frames are long, the
//     large array is expected), then the generic tick array from <IRDebug.h>.
// ja: AC用RAWスニペット: 位相整合キャリアの注意書き（ACフレームは長く配列が大きく
//     なるのは想定どおり）の後、<IRDebug.h> の汎用tick配列を出力する。
static void printRawSnippet(const esp32irpk::IRRawTickView &raw)
{
  Serial.println("// en: send with setPhaseAlignedCarrier(true) -- the safe AC default;");
  Serial.println("//     some vendors (e.g. Gree) drop frames on the hardware carrier.");
  Serial.println("// ja: setPhaseAlignedCarrier(true) で送ること（ACの安全な既定。");
  Serial.println("//     一部ベンダ（例: Gree）はハードウェアキャリアだとフレームが落ちる）。");
  esp32irpk::debug::printRawSendSnippet(Serial, raw);
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

  // en: decodeAny tries every built-in AC vendor and dumps the match via printTo.
  //     A recognized frame then prints two send-code forms: an editable setter
  //     template (lossy) and the compact bit-exact state bytes. An unrecognized
  //     remote falls back to the (long) RAW replay.
  // ja: decodeAny が全内蔵ACベンダを試し、一致を printTo で出力する。認識できたら
  //     2形式の送信コード（編集テンプレートの setter 版＝lossy、コンパクトで完全一致な
  //     状態バイト版）を出力。未対応リモコンは（長い）RAW replay にフォールバック。
  if (esp32irpk::ac::decodeAny(r.raw, &Serial) != esp32irpk::ac::AcVendor::UNKNOWN)
  {
    esp32irpk::ac::printSetterSnippet(r.raw, Serial); // editable template (lossy)
    esp32irpk::ac::printSendSnippet(r.raw, Serial);   // bit-exact state bytes
  }
  else
  {
    printRawSnippet(r.raw);
  }
  Serial.println();
}
