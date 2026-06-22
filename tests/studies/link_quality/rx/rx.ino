// Link-quality probe RX (ESP32IRPulseKit).
//
// Receives with the in-house decoder and dumps, for every frame, the decode
// result plus the raw edge widths (10us ticks) so the host monitor can score
// the live IR link environment while you move the boards by hand.
#include <ESP32IRPulseKit.h>

#ifndef IR_RX_GPIO
#define IR_RX_GPIO "4"
#endif

#ifndef IR_RX_INVERTED
#define IR_RX_INVERTED "1"
#endif

const int kIrRxGpio = atoi(IR_RX_GPIO);
const bool kIrRxInverted = atoi(IR_RX_INVERTED) != 0;

esp32irpk::IRReceiver<> rx(kIrRxGpio, kIrRxInverted);

namespace
{
void printRawTicks(const esp32irpk::IRRawTickView &raw)
{
  Serial.print(" ticks=");
  for (size_t i = 0; i < raw.len; ++i)
  {
    if (i > 0)
      Serial.print(",");
    Serial.print(raw.ticks[i]);
  }
}

void printBits64(uint64_t bits)
{
  Serial.print((uint32_t)(bits >> 32), HEX);
  Serial.print((uint32_t)(bits & 0xffffffffu), HEX);
}

void sendReady()
{
  Serial.print("RX_READY impl=ESP32IRPulseKit gpio=");
  Serial.print(kIrRxGpio);
  Serial.print(" inverted=");
  Serial.println(kIrRxInverted ? 1 : 0);
}

bool readLine(String &line)
{
  if (!Serial.available())
    return false;
  line = Serial.readStringUntil('\n');
  line.trim();
  return line.length() > 0;
}
} // namespace

void setup()
{
  Serial.begin(115200);
  delay(5000);

  if (!rx.begin())
  {
    Serial.println("RX_ERROR begin_failed");
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
      Serial.println("PONG");
    else if (line == "READY")
      sendReady();
  }

  esp32irpk::IRReceiveResult<> result;
  if (rx.read(result))
  {
    if (result.count == 0)
    {
      Serial.print("RX_RAW len=");
      Serial.print(result.raw.len);
      printRawTicks(result.raw);
      Serial.println();
    }
    else
    {
      const auto &candidate = result.candidates[0];
      Serial.print("RX_DECODE protocol=");
      Serial.print(candidate.name);
      Serial.print(" score=");
      Serial.print(candidate.score);
      Serial.print(" len=");
      Serial.print(candidate.decoded.bit_length);
      Serial.print(" bits=0x");
      printBits64(candidate.decoded.bits);
      Serial.print(" type=");
      Serial.print(candidate.decoded.isRepeat() ? "REPEAT" : "NORMAL");
      Serial.print(" raw_len=");
      Serial.print(result.raw.len);
      printRawTicks(result.raw);
      Serial.println();
    }
  }

  delay(1);
}
