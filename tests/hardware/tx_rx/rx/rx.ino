#include <ESP32IRPulseKit.h>

#ifndef IR_RX_GPIO
#define IR_RX_GPIO 32
#endif

#ifndef IR_RX_INVERTED
#define IR_RX_INVERTED 1
#endif

esp32irpk::IRReceiver rx(IR_RX_GPIO, IR_RX_INVERTED != 0);

namespace
{
void printBits64(uint64_t bits)
{
  Serial.print((uint32_t)(bits >> 32), HEX);
  Serial.print((uint32_t)(bits & 0xffffffffu), HEX);
}

void sendReady()
{
  Serial.print("RX_READY gpio=");
  Serial.print(IR_RX_GPIO);
  Serial.print(" inverted=");
  Serial.println(IR_RX_INVERTED);
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
    {
      Serial.println("PONG");
    }
    else if (line == "READY")
    {
      sendReady();
    }
  }

  esp32irpk::IRReceiveResult<> result;
  if (rx.read(result))
  {
    if (result.count == 0)
    {
      Serial.print("RX_RAW len=");
      Serial.println(result.raw.len);
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
      Serial.println(candidate.decoded.isRepeat() ? "REPEAT" : "NORMAL");
    }
  }

  delay(1);
}
