#include <ESP32IRPulseKit.h>

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

// en: All helpers live in an anonymous namespace. The Arduino .ino preprocessor
//     does not auto-generate prototypes for namespaced functions, which is what
//     the printAcFrame template needs (a generated prototype would drop the
//     `template` line and fail to compile). Define before use, call from loop().
// ja: ヘルパは全て無名名前空間に置く。Arduinoの.ino前処理は名前空間内の関数に自動
//     プロトタイプを生成しない。これがテンプレート printAcFrame に必要（生成された
//     プロトタイプは `template` 行を落として壊れる）。使用前に定義し、loop()から呼ぶ。
namespace
{
// en: Print a uint64_t as a valid C++ literal (e.g. 0x1234ULL).
// ja: uint64_tを有効なC++リテラル（例: 0x1234ULL）として出力する。
void printLiteral(uint64_t v)
{
  uint32_t hi = (uint32_t)(v >> 32);
  uint32_t lo = (uint32_t)(v & 0xFFFFFFFFu);
  Serial.print("0x");
  if (hi)
  {
    Serial.print(hi, HEX);
    char buf[9];
    snprintf(buf, sizeof(buf), "%08lx", (unsigned long)lo);
    Serial.print(buf);
  }
  else
  {
    Serial.print(lo, HEX);
  }
  Serial.print("ULL");
}

void printBits64(uint64_t bits)
{
  Serial.print((uint32_t)(bits >> 32), HEX);
  Serial.print((uint32_t)(bits & 0xFFFFFFFFu), HEX);
}

// en: Per-protocol named-field decode (generic remotes). Same as examples/01.
// ja: プロトコル別の名前付きフィールドデコード（汎用リモコン）。examples/01と同じ。
void printFrame(const esp32irpk::IRDecodedBits &b)
{
  switch (b.protocol_id)
  {
  case esp32irpk::IRProtocolID::NEC:
  {
    esp32irpk::frames::NECFrame f = esp32irpk::frames::NECFrame::fromBits(b);
    if (f.is_repeat)
    {
      Serial.println("  frame: NEC REPEAT");
      break;
    }
    Serial.print("  frame: NEC addr=0x");
    Serial.print(f.address, HEX);
    Serial.print(" cmd=0x");
    Serial.println(f.command, HEX);
    break;
  }
  case esp32irpk::IRProtocolID::AEHA:
  {
    esp32irpk::frames::AEHAFrame f = esp32irpk::frames::AEHAFrame::fromBits(b);
    if (f.is_repeat)
    {
      Serial.println("  frame: AEHA REPEAT");
      break;
    }
    Serial.print("  frame: AEHA bits=");
    Serial.print(f.bit_length);
    Serial.print(" data=0x");
    Serial.println((uint32_t)(f.data & 0xFFFFFFFFu), HEX);
    break;
  }
  case esp32irpk::IRProtocolID::SONY12:
  {
    esp32irpk::frames::Sony12Frame f = esp32irpk::frames::Sony12Frame::fromBits(b);
    if (f.is_repeat)
    {
      Serial.println("  frame: SONY12 REPEAT");
      break;
    }
    Serial.print("  frame: SONY12 data=0x");
    Serial.println(f.data, HEX);
    break;
  }
  case esp32irpk::IRProtocolID::SONY15:
  {
    esp32irpk::frames::Sony15Frame f = esp32irpk::frames::Sony15Frame::fromBits(b);
    if (f.is_repeat)
    {
      Serial.println("  frame: SONY15 REPEAT");
      break;
    }
    Serial.print("  frame: SONY15 data=0x");
    Serial.println(f.data, HEX);
    break;
  }
  case esp32irpk::IRProtocolID::SONY20:
  {
    esp32irpk::frames::Sony20Frame f = esp32irpk::frames::Sony20Frame::fromBits(b);
    if (f.is_repeat)
    {
      Serial.println("  frame: SONY20 REPEAT");
      break;
    }
    Serial.print("  frame: SONY20 data=0x");
    Serial.println(f.data, HEX);
    break;
  }
  case esp32irpk::IRProtocolID::SAMSUNG32:
  {
    esp32irpk::frames::Samsung32Frame f = esp32irpk::frames::Samsung32Frame::fromBits(b);
    if (f.is_repeat)
    {
      Serial.println("  frame: SAMSUNG32 REPEAT");
      break;
    }
    Serial.print("  frame: SAMSUNG32 addr=0x");
    Serial.print(f.address, HEX);
    Serial.print(" cmd=0x");
    Serial.println(f.command, HEX);
    break;
  }
  case esp32irpk::IRProtocolID::SAMSUNG36:
  {
    esp32irpk::frames::Samsung36Frame f = esp32irpk::frames::Samsung36Frame::fromBits(b);
    if (f.is_repeat)
    {
      Serial.println("  frame: SAMSUNG36 REPEAT");
      break;
    }
    Serial.print("  frame: SAMSUNG36 addr=0x");
    Serial.print(f.address, HEX);
    Serial.print(" cmd=0x");
    Serial.println(f.command, HEX);
    break;
  }
  case esp32irpk::IRProtocolID::JVC:
  {
    esp32irpk::frames::JVCFrame f = esp32irpk::frames::JVCFrame::fromBits(b);
    if (f.is_repeat)
    {
      Serial.println("  frame: JVC REPEAT");
      break;
    }
    Serial.print("  frame: JVC addr=0x");
    Serial.print(f.address, HEX);
    Serial.print(" cmd=0x");
    Serial.println(f.command, HEX);
    break;
  }
  case esp32irpk::IRProtocolID::RC5:
  {
    esp32irpk::frames::RC5Frame f = esp32irpk::frames::RC5Frame::fromBits(b);
    Serial.print("  frame: RC5 data=0x");
    Serial.println(f.data, HEX);
    break;
  }
  case esp32irpk::IRProtocolID::RC6_M0_16:
  {
    esp32irpk::frames::RC6M0Frame f = esp32irpk::frames::RC6M0Frame::fromBits(b);
    Serial.print("  frame: RC6_M0_16 data=0x");
    Serial.println(f.data, HEX);
    break;
  }
  case esp32irpk::IRProtocolID::RC6_M6_32:
  {
    esp32irpk::frames::RC6M6Frame f = esp32irpk::frames::RC6M6Frame::fromBits(b);
    Serial.print("  frame: RC6_M6_32 data=0x");
    Serial.println((uint32_t)(f.data & 0xFFFFFFFFu), HEX);
    break;
  }
  default:
    break;
  }
}

// en: print "power/mode/temp/fan/checksum" plus the full state in hex. The hex
//     line is handy for inspecting fields the named accessors do not expose yet
//     (e.g. half-degree or vendor flags). Shared by all AC vendors.
// ja: power/mode/temp/fan/checksum とデコード状態全体のhexを出力する。hex行は
//     名前付きアクセサが未公開のフィールド（0.5℃やベンダ固有フラグ等）の確認に便利。
//     全ACベンダ共通。
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

// en: Try each AC vendor on the RAW capture and print the decoded settings as a
//     comment. Vendors are added incrementally; when none match, the RAW replay
//     snippet still reproduces the frame.
// ja: RAWキャプチャに各ACベンダを試し、デコードした設定をコメント出力する。ベンダは
//     順次追加。どれにも当たらなくても下のRAWスニペットでフレームは再現できる。
void printAcDecode(const esp32irpk::IRRawTickView &raw)
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
  esp32irpk::ac::Mitsubishi::Frame mf;
  if (esp32irpk::ac::Mitsubishi::Frame::fromRaw(raw, mf))
  {
    printAcFrame("Mitsubishi", mf);
    return;
  }
  Serial.println("// decoded: no AC vendor matched (raw replay still works)");
}

// en: Decoded protocols: print an IRDecodedBits snippet. Uniform across every
//     protocol and re-sends the exact decoded payload.
// ja: デコードできたプロトコル: IRDecodedBitsのスニペットを出力する。全protocol共通の
//     形で、デコードしたペイロードをそのまま再送できる。
void printBitsSnippet(const esp32irpk::IRDecodeCandidate &c)
{
  const esp32irpk::IRDecodedBits &b = c.decoded;
  Serial.println("// send code (decoded):");
  Serial.println("esp32irpk::IRDecodedBits bits{};");
  Serial.print("bits.protocol_id = esp32irpk::IRProtocolID::");
  Serial.print(c.name); // en: spec name matches the enum id / ja: spec名はenum idと一致
  Serial.println(";");
  Serial.print("bits.frame_type = esp32irpk::IRFrameType::");
  Serial.println(b.frame_type == esp32irpk::IRFrameType::REPEAT ? "REPEAT;" : "NORMAL;");
  Serial.print("bits.bit_length = ");
  Serial.print((unsigned)b.bit_length);
  Serial.println(";");
  Serial.print("bits.bits = ");
  printLiteral(b.bits);
  Serial.println(";");
  Serial.println("tx.send(bits);");
}

// en: Print the captured RAW as a ready-to-send tick array. AC frames are long,
//     so this array is large -- that is expected.
// ja: キャプチャしたRAWを送信用のtick配列として出力する。ACフレームは長いので配列は
//     大きくなる（想定どおり）。
void printRawSnippet(const esp32irpk::IRRawTickView &raw)
{
  Serial.println("// send code (raw replay):");
  Serial.println("// en: send AC frames with setPhaseAlignedCarrier(true) -- the safe AC");
  Serial.println("//     default; some vendors (e.g. Gree) drop frames on the HW carrier.");
  Serial.println("// ja: ACフレームは setPhaseAlignedCarrier(true) で送ること（ACの安全な");
  Serial.println("//     既定。一部ベンダ（例: Gree）はHWキャリアだとフレームが落ちる）。");
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

  Serial.print("raw(us):");
  for (size_t i = 0; i < r.raw.len; ++i)
  {
    Serial.print(' ');
    Serial.print((unsigned)(r.raw.ticks[i] * 10)); // en: ticks are 10us / ja: tickは10us
  }
  Serial.println();

  // en: generic decoded candidates / ja: 汎用デコード候補
  Serial.println("-- decoded candidates --");
  if (r.count == 0)
  {
    Serial.println("(none)");
  }
  for (uint8_t i = 0; i < r.count; ++i)
  {
    const esp32irpk::IRDecodeCandidate &c = r.candidates[i];
    const esp32irpk::IRDecodedBits &b = c.decoded;
    Serial.print("#");
    Serial.print(i);
    Serial.print(" pid=");
    Serial.print((unsigned)c.protocol_id);
    Serial.print(" protocol=");
    Serial.print(c.name);
    Serial.print(" score=");
    Serial.print((int)c.score);
    Serial.print(" len=");
    Serial.print((unsigned)b.bit_length);
    Serial.print(" bits=0x");
    printBits64(b.bits);
    Serial.print(" frame_type=");
    Serial.println(b.frame_type == esp32irpk::IRFrameType::REPEAT ? "REPEAT" : "NORMAL");
    printFrame(b);
  }

  // en: AC vendor decode (heat-pump remotes) / ja: ACベンダデコード（エアコンリモコン）
  Serial.println("-- AC vendor decode --");
  printAcDecode(r.raw);

  // en: copy-paste re-send code / ja: 貼り付け再送コード
  Serial.println("-- send code --");
  if (r.count > 0)
  {
    printBitsSnippet(r.candidates[0]);
  }
  printRawSnippet(r.raw);
  Serial.println();
}
