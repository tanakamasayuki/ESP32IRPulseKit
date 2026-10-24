// en: Capture and learn an air-conditioner state
// ja: エアコンの状態信号を受信して学習する
#include <ESP32IRPulseKit.h>
// en: Optional Serial-formatting and generated-code helpers.
// ja: シリアル表示とコード生成用の任意ヘルパーです。
#include <IRDebug.h>

// en: Match this GPIO and input polarity to the receiver module OUT signal.
// ja: GPIOと入力極性を受信モジュールのOUT信号に合わせます。
esp32irpk::IRReceiver rx(32, true);

// en: Keep helpers defined before use; the README explains the Arduino preprocessing detail.
// ja: ヘルパーを使用前に定義します。Arduino前処理との関係はREADMEを参照してください。
namespace
{
// en: Print reusable code only; this helper does not transmit an IR signal.
// ja: 再利用するコードを表示するだけで、この関数から赤外線は送信しません。
static void printRawSnippet(const esp32irpk::IRRawTickView &raw)
{
  Serial.println("// en: send with setPhaseAlignedCarrier(true) -- the safe AC default;");
  Serial.println("//     some vendors (e.g. Gree) drop frames on the hardware carrier.");
  Serial.println("// ja: setPhaseAlignedCarrier(true) で送ること（ACの安全な既定。");
  Serial.println("//     一部ベンダ（例: Gree）はハードウェアキャリアだとフレームが落ちる）。");
  esp32irpk::debug::printRawSendSnippet(Serial, raw);
}
}

void setup()
{
  Serial.begin(115200);

  // en: Capture RAW only; a zero candidate count is intentional in this mode.
  // ja: RAWのみ受信します。このモードで候補数が0になるのは意図どおりです。
  rx.setDecodeCandidates(0);
  // en: Reserve room for a long AC burst before begin(); inspect truncation after reception.
  // ja: begin()前に長いAC信号用の容量を確保し、受信後に切り詰めの有無を確認します。
  rx.setMaxRxSymbols(1024);
  // en: Silence ends capture; keep internal AC gaps inside the same capture (see README).
  // ja: 無信号時間で受信を終了します。AC信号内部の隙間で分割しないようにします（README参照）。
  rx.setIdleThresholdUs(100000);
  // en: Start capture after applying settings; check the bool result when adapting this example.
  // ja: 設定後に受信を開始します。組み込む際はboolの戻り値も確認します。
  rx.begin();
}

void loop()
{
  esp32irpk::IRReceiveResult<> r;
  // en: No completed capture is available yet; yield briefly and try again on the next loop.
  // ja: 受信結果がまだない場合は少し待ち、次のloopで再度確認します。
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

  // en: Try AC state decoders on the RAW capture; unrecognized data falls back to RAW code.
  // ja: RAW波形をAC状態デコーダで調べ、未認識の場合はRAWコードに進みます。
  if (esp32irpk::ac::decodeAny(r.raw, &Serial) != esp32irpk::ac::AcVendor::UNKNOWN)
  {
    // en: Editable logical fields may omit settings; use state bytes below for exact decoded-state replay.
    // ja: 論理項目の編集例は一部設定を省く場合があります。状態の再現には下の状態バイト版を使います。
    esp32irpk::ac::printSetterSnippet(r.raw, Serial);
    esp32irpk::ac::printSendSnippet(r.raw, Serial);
  }
  else
  {
    printRawSnippet(r.raw);
  }
  Serial.println();
}
