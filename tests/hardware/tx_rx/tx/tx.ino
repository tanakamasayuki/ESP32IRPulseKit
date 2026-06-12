#include <ESP32IRPulseKit.h>

#ifndef IR_TX_GPIO
#define IR_TX_GPIO 4
#endif

#ifndef IR_TX_INVERTED
#define IR_TX_INVERTED 0
#endif

esp32irpk::IRSender tx(IR_TX_GPIO, IR_TX_INVERTED != 0);

namespace
{
void sendReady()
{
  Serial.print("TX_READY gpio=");
  Serial.print(IR_TX_GPIO);
  Serial.print(" inverted=");
  Serial.println(IR_TX_INVERTED);
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

void handleCommand(const String &line)
{
  if (line == "PING")
  {
    Serial.println("PONG");
    return;
  }

  if (line == "SEND NEC 00ff 34")
  {
    bool ok = tx.sendNEC(0x00ff, 0x34);
    Serial.println(ok ? "TX_OK NEC 00ff 34" : "TX_ERROR send_failed");
    return;
  }

  Serial.print("TX_ERROR unknown_command ");
  Serial.println(line);
}
} // namespace

void setup()
{
  Serial.begin(115200);
  delay(5000);
  if (!tx.begin())
  {
    Serial.println("TX_ERROR begin_failed");
    return;
  }
  sendReady();
}

void loop()
{
  String line;
  if (readLine(line))
  {
    handleCommand(line);
  }
  delay(1);
}
