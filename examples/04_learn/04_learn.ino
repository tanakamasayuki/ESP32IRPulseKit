#include <ESP32IRPulseKit.h>

// en: Learn a remote: receive a signal and print copy-paste C++ to re-send it.
//     RX only, so it runs on a single board (TX is usually a separate device).
// ja: リモコンの学習: 信号を受信し、再送するためのC++コードを貼り付け可能な形で
//     出力します。受信のみなので1台で動きます（送信側は別デバイスが普通）。
// en: Adjust the GPIO number to your board wiring.
// ja: GPIO番号はご利用環境の配線に合わせて変更してください。
esp32irpk::IRReceiver rx(32, true); // en: most receiver modules output inverted / ja: 多くの受信モジュールは出力反転

// en: Print a uint64_t as a valid C++ literal (e.g. 0x1234ULL).
// ja: uint64_tを有効なC++リテラル（例: 0x1234ULL）として出力します。
static void printLiteral(uint64_t v)
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

// en: Decoded protocols: print an IRDecodedBits snippet. It is uniform across
//     every protocol and re-sends the exact decoded payload.
// ja: デコードできたプロトコル: IRDecodedBitsのスニペットを出力します。全protocol
//     共通の形で、デコードしたペイロードをそのまま再送できます。
static void printBitsSnippet(const esp32irpk::IRDecodeCandidate &c)
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

// en: Unrecognized signals: print a RAW tick array. Works for any waveform.
// ja: 認識できなかった信号: RAW tick配列を出力します。あらゆる波形に有効です。
static void printRawSnippet(const esp32irpk::IRRawTickView &raw)
{
  Serial.println("// send code (raw replay):");
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
  if (const esp32irpk::IRDecodeCandidate *c = r.candidate())
  {
    Serial.print("protocol=");
    Serial.print(c->name);
    Serial.print(" score=");
    Serial.println((int)c->score);
    printBitsSnippet(*c);
  }
  else
  {
    Serial.println("protocol=unknown");
    printRawSnippet(r.raw);
  }
  Serial.println();
}
