#include <ESP32IRPulseKit.h>

// en: Simple RX example that prints decoded candidates to Serial.
// ja: 受信した信号をデコードしてシリアルに出力する簡単な例。
// en: Adjust GPIO numbers to your board wiring.
// ja: GPIO番号はご利用環境の配線に合わせて変更してください。
esp32irpk::IRReceiver rx(32, true); // en: common IR receiver modules output inverted / ja: よく使われる受信モジュールは出力が反転

static void printFrame(const esp32irpk::IRDecodedBits &b)
{
  using namespace esp32irpk;
  switch (b.protocol_id)
  {
  case IRProtocolID::NEC:
  {
    frames::NECFrame f = frames::NECFrame::fromBits(b);
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
  case IRProtocolID::SONY12:
  {
    frames::Sony12Frame f = frames::Sony12Frame::fromBits(b);
    if (f.is_repeat)
    {
      Serial.println("  frame: SONY12 REPEAT");
      break;
    }
    Serial.print("  frame: SONY12 data=0x");
    Serial.println(f.data, HEX);
    break;
  }
  case IRProtocolID::SONY15:
  {
    frames::Sony15Frame f = frames::Sony15Frame::fromBits(b);
    if (f.is_repeat)
    {
      Serial.println("  frame: SONY15 REPEAT");
      break;
    }
    Serial.print("  frame: SONY15 data=0x");
    Serial.println(f.data, HEX);
    break;
  }
  case IRProtocolID::SONY20:
  {
    frames::Sony20Frame f = frames::Sony20Frame::fromBits(b);
    if (f.is_repeat)
    {
      Serial.println("  frame: SONY20 REPEAT");
      break;
    }
    Serial.print("  frame: SONY20 data=0x");
    Serial.println(f.data, HEX);
    break;
  }
  case IRProtocolID::SAMSUNG32:
  {
    frames::Samsung32Frame f = frames::Samsung32Frame::fromBits(b);
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
  case IRProtocolID::SAMSUNG36:
  {
    frames::Samsung36Frame f = frames::Samsung36Frame::fromBits(b);
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
  default:
    break;
  }
}

void setup()
{
  Serial.begin(115200);
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

  if (r.count == 0)
  {
    Serial.println("no decoded candidates");
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
  }
}
