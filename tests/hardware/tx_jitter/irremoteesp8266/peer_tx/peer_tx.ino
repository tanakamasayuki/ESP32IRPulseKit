// Compat peer TX: drives the IR LED with IRremoteESP8266 instead of
// ESP32IRPulseKit. The serial command protocol matches protocol_matrix's peer
// (SEND <PROTOCOL> <hex>, PING/READY) so the same pytest harness can compare
// how ESP32IRPulseKit RX decodes frames produced by an external library.
#include <Arduino.h>
#include <IRsend.h>

#ifndef IR_TX_GPIO
#define IR_TX_GPIO "4"
#endif

#ifndef IR_TX_INVERTED
#define IR_TX_INVERTED "0"
#endif

const int kIrTxGpio = atoi(IR_TX_GPIO);
const bool kIrTxInverted = atoi(IR_TX_INVERTED) != 0;

IRsend irsend(kIrTxGpio, kIrTxInverted);

namespace
{
bool readLine(String &line)
{
  if (!Serial.available())
    return false;
  line = Serial.readStringUntil('\n');
  line.trim();
  return line.length() > 0;
}

bool parseHex64(const String &text, uint64_t &value)
{
  if (text.length() == 0)
    return false;

  char *end = nullptr;
  value = strtoull(text.c_str(), &end, 16);
  return end != text.c_str() && *end == '\0';
}

bool nextToken(const String &line, int &pos, String &token)
{
  while (pos < line.length() && line[pos] == ' ')
    pos++;
  if (pos >= line.length())
    return false;

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

void sendReady()
{
  Serial.print("TX_READY impl=IRremoteESP8266 gpio=");
  Serial.print(kIrTxGpio);
  Serial.print(" inverted=");
  Serial.println(kIrTxInverted ? 1 : 0);
}

void printTxOk(const String &protocolText, const String &bitsText)
{
  Serial.print("TX_OK ");
  Serial.print(protocolText);
  Serial.print(" ");
  Serial.println(bitsText);
}

// Send a single frame (repeat=0) per command so the RX side observes exactly
// one decode. Bit order and framing follow IRremoteESP8266 conventions, which
// is the difference compat_matrix is meant to surface.
void handleSend(const String &line)
{
  int pos = String("SEND").length();
  String protocolText;
  String bitsText;
  if (!nextToken(line, pos, protocolText) || !nextToken(line, pos, bitsText))
  {
    Serial.println("TX_ERROR invalid_send_command");
    return;
  }

  uint64_t bits = 0;
  if (!parseHex64(bitsText, bits))
  {
    Serial.println("TX_ERROR invalid_bits");
    return;
  }

  if (protocolText == "NEC")
  {
    irsend.sendNEC(bits, 32);
  }
  else if (protocolText == "SONY12")
  {
    irsend.sendSony(bits, 12, 0);
  }
  else if (protocolText == "SAMSUNG32")
  {
    irsend.sendSAMSUNG(bits, 32);
  }
  else if (protocolText == "JVC24")
  {
    irsend.sendJVC(bits, 24);
  }
  else
  {
    Serial.println("TX_ERROR unsupported_protocol");
    return;
  }

  printTxOk(protocolText, bitsText);
}

void handleCommand(const String &line)
{
  if (line == "PING")
  {
    Serial.println("PONG");
    return;
  }
  if (line == "READY")
  {
    sendReady();
    return;
  }
  if (line.startsWith("SEND "))
  {
    handleSend(line);
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
  irsend.begin();
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
