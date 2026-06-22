#include <ESP32IRPulseKit.h>

// en: Simple RX example that prints decoded candidates to Serial.
// ja: 受信した信号をデコードしてシリアルに出力する簡単な例。
// en: Adjust GPIO numbers to your board wiring.
// ja: GPIO番号はご利用環境の配線に合わせて変更してください。
esp32irpk::IRReceiver rx(32, true); // en: common IR receiver modules output inverted / ja: よく使われる受信モジュールは出力が反転

void setup()
{
  Serial.begin(115200);
  // rx.setScoreThreshold(-32768); // en: show negative-score candidates / ja: 負スコア候補も表示する
  rx.begin();
}

void loop()
{
  esp32irpk::IRReceiveResult r;
  if (!rx.read(r))
  {
    delay(1);
    return;
  }

  Serial.println("---- IR received ----");
  Serial.print("raw.len(ticks)=");
  Serial.print((unsigned)r.raw.len);
  Serial.print(" flags=0x");
  Serial.println((unsigned)r.flags, HEX);
  Serial.print("raw (us):");
  for (size_t i = 0; i < r.raw.len; ++i)
  {
    Serial.print(" ");
    Serial.print((unsigned)(r.raw.ticks[i] * 10)); // en: ticks are 10us / ja: tickは10us
  }
  Serial.println();

  if (r.count == 0)
  {
    Serial.println("no decoded candidates");
    Serial.println();
    return;
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
    Serial.print((uint32_t)(b.bits >> 32), HEX);
    Serial.print((uint32_t)(b.bits & 0xFFFFFFFFu), HEX);
    Serial.print(" frame_type=");
    Serial.println(b.frame_type == esp32irpk::IRFrameType::REPEAT ? "REPEAT" : "NORMAL");
    printFrame(b);
    Serial.println();
  }
}

static void printFrame(const esp32irpk::IRDecodedBits &b)
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
