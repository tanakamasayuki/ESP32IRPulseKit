#include <ESP32IRPulseKit.h>
#include <IRDebug.h> // en: shared Serial-formatting helpers / ja: 共有のシリアル整形ヘルパー

// en: Manual IR dump tool. Receives a signal and prints everything useful in one
//     shot: the RAW waveform, every decoded candidate (generic protocols), an AC
//     vendor decode when a heat-pump frame matches, and copy-paste C++ to re-send
//     it. RX only, so it runs on a single board. Generic remotes and AC remotes
//     are handled by the SAME sketch: decode candidates stay enabled (generic),
//     while the RAW path is given AC-sized capacity and a long idle so a
//     multi-frame AC burst is captured whole and tried against the AC vendors.
// ja: 手動IRダンプツール。信号を受信し、一度に有用な情報を全部出力する: RAW波形、
//     全デコード候補（汎用プロトコル）、ACフレームに当たればベンダデコード、そして
//     再送用の貼り付け可能なC++。受信のみなので1台で動く。汎用リモコンもエアコンも
//     同じスケッチで扱う: デコード候補は有効のまま（汎用）、RAW経路にはAC向けの大きな
//     容量と長いidleを与え、複数フレームのACバーストを丸ごと捕えてACベンダに当てる。
//
// en: Most of the per-line formatting is shared with the examples via <IRDebug.h>
//     (RAW dump, candidate line, named-field decode, send snippets).
// ja: 行ごとの整形の大半は <IRDebug.h> で examples と共有している（RAWダンプ、候補
//     行、名前付きフィールドデコード、送信スニペット）。
//
// en: Config comes from build defines (set from tests/.env by dump.py). Override
//     IR_RX_IDLE_US lower (e.g. 35000) for snappier generic-only capture, or
//     raise it if one AC press splits into several "==== IR DUMP ====" blocks.
// ja: 設定はビルドdefineから入る（dump.pyがtests/.envから設定）。汎用のみで反応を
//     早くしたいときはIR_RX_IDLE_USを下げる（例: 35000）。1回のAC押下が複数の
//     "==== IR DUMP ====" に割れるときは上げる。

#ifndef IR_RX_GPIO
#define IR_RX_GPIO "32"
#endif
#ifndef IR_RX_INVERTED
#define IR_RX_INVERTED "1" // en: most receiver modules output inverted / ja: 多くの受信モジュールは出力反転
#endif
#ifndef IR_RX_IDLE_US
#define IR_RX_IDLE_US "100000" // en: 100ms, AC-safe (ceiling ~300ms) / ja: 100ms、AC安全（上限~300ms）
#endif
#ifndef IR_RX_MAX_SYMBOLS
#define IR_RX_MAX_SYMBOLS "1024" // en: AC frames are long / ja: ACフレームは長い
#endif

const int kIrRxGpio = atoi(IR_RX_GPIO);
const bool kIrRxInverted = atoi(IR_RX_INVERTED) != 0;
const uint32_t kIdleUs = (uint32_t)strtoul(IR_RX_IDLE_US, nullptr, 10);
const size_t kMaxSymbols = (size_t)strtoul(IR_RX_MAX_SYMBOLS, nullptr, 10);

esp32irpk::IRReceiver rx(kIrRxGpio, kIrRxInverted);

// en: Helpers live in an anonymous namespace and are defined before use, so the
//     Arduino .ino preprocessor does not need to auto-generate prototypes for
//     them. Call them from loop().
// ja: ヘルパは無名名前空間に置き使用前に定義する。こうすればArduinoの.ino前処理が
//     自動プロトタイプを生成する必要がない。loop()から呼ぶ。
namespace
{
// en: Try each AC vendor on the RAW capture and, on a match, let the Frame dump
//     itself via Frame::printTo(Print&) — the common power/mode/temp/fan/checksum
//     line, the vendor's own fields (louver/swing/vane), and the full hex state.
//     Vendors are added incrementally; when none match, the RAW replay snippet
//     still reproduces the frame.
// ja: RAWキャプチャに各ACベンダを試し、一致したら Frame::printTo(Print&) に自分を
//     ダンプさせる。共通の power/mode/temp/fan/checksum 行＋ベンダ固有フィールド
//     （louver/swing/vane）＋状態全体のhexを出力。ベンダは順次追加。どれにも当たら
//     なくても下のRAWスニペットでフレームは再現できる。
void printAcDecode(const esp32irpk::IRRawTickView &raw)
{
  esp32irpk::ac::Panasonic::Frame pf;
  if (esp32irpk::ac::Panasonic::Frame::fromRaw(raw, pf))
  {
    pf.printTo(Serial);
    return;
  }
  esp32irpk::ac::Gree::Frame gf;
  if (esp32irpk::ac::Gree::Frame::fromRaw(raw, gf))
  {
    gf.printTo(Serial);
    return;
  }
  esp32irpk::ac::Mitsubishi::Frame mf;
  if (esp32irpk::ac::Mitsubishi::Frame::fromRaw(raw, mf))
  {
    mf.printTo(Serial);
    return;
  }
  Serial.println("// decoded: no AC vendor matched (raw replay still works)");
}

// en: AC RAW snippet: a phase-aligned-carrier reminder (AC frames are long, the
//     large array is expected), then the generic tick array from <IRDebug.h>.
// ja: AC用RAWスニペット: 位相整合キャリアの注意書き（ACフレームは長く配列が大きく
//     なるのは想定どおり）の後、<IRDebug.h> の汎用tick配列を出力する。
void printRawSnippet(const esp32irpk::IRRawTickView &raw)
{
  Serial.println("// en: send AC frames with setPhaseAlignedCarrier(true) -- the safe AC");
  Serial.println("//     default; some vendors (e.g. Gree) drop frames on the HW carrier.");
  Serial.println("// ja: ACフレームは setPhaseAlignedCarrier(true) で送ること（ACの安全な");
  Serial.println("//     既定。一部ベンダ（例: Gree）はHWキャリアだとフレームが落ちる）。");
  esp32irpk::debug::printRawSendSnippet(Serial, raw);
}

void sendReady()
{
  Serial.print("DUMP_READY gpio=");
  Serial.print(kIrRxGpio);
  Serial.print(" inverted=");
  Serial.print(kIrRxInverted ? 1 : 0);
  Serial.print(" idle_us=");
  Serial.print((unsigned long)kIdleUs);
  Serial.print(" max_symbols=");
  Serial.println((unsigned long)kMaxSymbols);
}

bool readLine(String &line)
{
  if (!Serial.available())
  {
    return false;
  }
  line = Serial.readStringUntil('\n');
  line.trim();
  return line.length() > 0;
}
} // namespace

void setup()
{
  Serial.begin(115200);
  delay(2000);

  // en: decode candidates stay ON so generic remotes decode; the wider RAW
  //     capacity and long idle let a multi-frame AC burst land in one capture.
  // ja: 汎用リモコンをデコードするため候補は有効のまま。広いRAW容量と長いidleで
  //     複数フレームのACバーストを1キャプチャに収める。
  rx.setMaxRxSymbols(kMaxSymbols);
  rx.setIdleThresholdUs(kIdleUs);
  if (!rx.begin())
  {
    Serial.println("DUMP_ERROR begin_failed (check GPIO / wiring)");
    return;
  }
  sendReady();
}

void loop()
{
  String line;
  if (readLine(line))
  {
    if (line == "PING")
    {
      Serial.println("PONG");
    }
    else if (line == "READY")
    {
      sendReady();
    }
  }

  esp32irpk::IRReceiveResult<> r;
  if (!rx.read(r))
  {
    delay(1);
    return;
  }

  Serial.println("==== IR DUMP ====");
  Serial.print("raw.len(ticks)=");
  Serial.print((unsigned)r.raw.len);
  Serial.print(" flags=0x");
  Serial.print((unsigned)r.flags, HEX);
  if (r.truncated())
  {
    Serial.print("  (TRUNCATED -- increase IR_RX_MAX_SYMBOLS)");
  }
  Serial.println();

  esp32irpk::debug::printRawMicros(Serial, r.raw);

  // en: generic decoded candidates / ja: 汎用デコード候補
  Serial.println("-- decoded candidates --");
  if (r.count == 0)
  {
    Serial.println("(none)");
  }
  for (uint8_t i = 0; i < r.count; ++i)
  {
    const esp32irpk::IRDecodeCandidate &c = r.candidates[i];
    esp32irpk::debug::printDecodedCandidate(Serial, i, c);
    esp32irpk::debug::printDecodedFrame(Serial, c.decoded);
  }

  // en: AC vendor decode (heat-pump remotes) / ja: ACベンダデコード（エアコンリモコン）
  Serial.println("-- AC vendor decode --");
  printAcDecode(r.raw);

  // en: copy-paste re-send code / ja: 貼り付け再送コード
  Serial.println("-- send code --");
  if (r.count > 0)
  {
    esp32irpk::debug::printBitsSendSnippet(Serial, r.candidates[0]);
  }
  printRawSnippet(r.raw);
  Serial.println();
}
