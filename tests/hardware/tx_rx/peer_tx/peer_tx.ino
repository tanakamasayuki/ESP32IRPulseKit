#include <ESP32IRPulseKit.h>

#ifndef IR_TX_GPIO
#define IR_TX_GPIO "4"
#endif

#ifndef IR_TX_INVERTED
#define IR_TX_INVERTED "0"
#endif

const int kIrTxGpio = atoi(IR_TX_GPIO);
const bool kIrTxInverted = atoi(IR_TX_INVERTED) != 0;

esp32irpk::IRSender tx(kIrTxGpio, kIrTxInverted);

namespace
{
bool g_loopEnabled = false;
uint16_t g_loopAddress = 0x00ff;
uint8_t g_loopCommand = 0x34;
uint32_t g_loopIntervalMs = 250;
uint32_t g_nextLoopSendMs = 0;

void sendReady()
{
  Serial.print("TX_READY gpio=");
  Serial.print(kIrTxGpio);
  Serial.print(" inverted=");
  Serial.println(kIrTxInverted ? 1 : 0);
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

bool parseDecimal(const String &text, uint32_t &value)
{
  if (text.length() == 0)
  {
    return false;
  }

  char *end = nullptr;
  value = strtoul(text.c_str(), &end, 10);
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

bool sendNec(uint16_t address, uint8_t command)
{
  esp32irpk::frames::NECFrame frame{};
  frame.address = address;
  frame.command = command;
  return tx.send(frame.toBits(), 0);
}

void printNecOk(const char *prefix, const String &addressText, const String &commandText)
{
  Serial.print(prefix);
  Serial.print(" NEC ");
  Serial.print(addressText);
  Serial.print(" ");
  Serial.println(commandText);
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

  bool ok = sendNec(static_cast<uint16_t>(address), static_cast<uint8_t>(command));
  if (!ok)
  {
    Serial.println("TX_ERROR send_failed");
    return;
  }

  printNecOk("TX_OK", addressText, commandText);
}

void handleLoopNec(const String &line)
{
  int pos = String("LOOP NEC").length();
  String addressText;
  String commandText;
  String intervalText;
  if (!nextToken(line, pos, addressText) || !nextToken(line, pos, commandText))
  {
    Serial.println("TX_ERROR invalid_loop_nec_command");
    return;
  }

  uint32_t address = 0;
  uint32_t command = 0;
  if (!parseHex(addressText, address) || address > 0xffff || !parseHex(commandText, command) || command > 0xff)
  {
    Serial.println("TX_ERROR invalid_nec_value");
    return;
  }

  uint32_t interval = 250;
  if (nextToken(line, pos, intervalText))
  {
    uint32_t parsed = 0;
    if (!parseDecimal(intervalText, parsed) || parsed < 50 || parsed > 10000)
    {
      Serial.println("TX_ERROR invalid_loop_interval");
      return;
    }
    interval = parsed;
  }

  g_loopAddress = static_cast<uint16_t>(address);
  g_loopCommand = static_cast<uint8_t>(command);
  g_loopIntervalMs = interval;
  g_nextLoopSendMs = 0;
  g_loopEnabled = true;

  printNecOk("TX_LOOP", addressText, commandText);
  Serial.print("TX_LOOP_INTERVAL ");
  Serial.println(g_loopIntervalMs);
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

  if (line.startsWith("LOOP NEC "))
  {
    handleLoopNec(line);
    return;
  }

  if (line == "STOP")
  {
    g_loopEnabled = false;
    Serial.println("TX_STOPPED");
    return;
  }

  Serial.print("TX_ERROR unknown_command ");
  Serial.println(line);
}

void serviceLoop()
{
  if (!g_loopEnabled)
  {
    return;
  }

  uint32_t now = millis();
  if (g_nextLoopSendMs != 0 && static_cast<int32_t>(now - g_nextLoopSendMs) < 0)
  {
    return;
  }

  bool ok = sendNec(g_loopAddress, g_loopCommand);
  Serial.println(ok ? "TX_LOOP_SENT" : "TX_ERROR loop_send_failed");
  g_nextLoopSendMs = now + g_loopIntervalMs;
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
  serviceLoop();
  delay(1);
}
