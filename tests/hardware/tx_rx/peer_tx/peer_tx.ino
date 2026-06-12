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

bool parseHex(const String &text, uint32_t &value)
{
  if (text.length() == 0)
  {
    return false;
  }

  char *end = nullptr;
  value = strtoul(text.c_str(), &end, 16);
  return end != text.c_str() && *end == '\0';
}

bool nextToken(const String &line, int &pos, String &token)
{
  while (pos < line.length() && line[pos] == ' ')
  {
    pos++;
  }
  if (pos >= line.length())
  {
    return false;
  }

  int end = line.indexOf(' ', pos);
  if (end < 0)
  {
    token = line.substring(pos);
    pos = line.length();
  }
  else
  {
    token = line.substring(pos, end);
    pos = end + 1;
  }
  return true;
}

void handleSendNec(const String &line)
{
  int pos = String("SEND NEC").length();
  String addressText;
  String commandText;
  if (!nextToken(line, pos, addressText) || !nextToken(line, pos, commandText))
  {
    Serial.println("TX_ERROR invalid_nec_command");
    return;
  }

  uint32_t address = 0;
  uint32_t command = 0;
  if (!parseHex(addressText, address) || address > 0xffff || !parseHex(commandText, command) || command > 0xff)
  {
    Serial.println("TX_ERROR invalid_nec_value");
    return;
  }

  bool ok = tx.sendNEC(static_cast<uint16_t>(address), static_cast<uint8_t>(command));
  if (!ok)
  {
    Serial.println("TX_ERROR send_failed");
    return;
  }

  Serial.print("TX_OK NEC ");
  Serial.print(addressText);
  Serial.print(" ");
  Serial.println(commandText);
}

void handleCommand(const String &line)
{
  if (line == "PING")
  {
    Serial.println("PONG");
    return;
  }

  if (line.startsWith("SEND NEC "))
  {
    handleSendNec(line);
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
