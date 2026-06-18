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
  Serial.print("TX_READY impl=ESP32IRPulseKit gpio=");
  Serial.print(kIrTxGpio);
  Serial.print(" inverted=");
  Serial.println(kIrTxInverted ? 1 : 0);
}

bool sendBits(esp32irpk::IRProtocolID protocol, uint16_t bitLength, uint64_t bits)
{
  esp32irpk::IRDecodedBits decoded{};
  decoded.protocol_id = protocol;
  decoded.frame_type = esp32irpk::IRFrameType::NORMAL;
  decoded.bit_length = bitLength;
  decoded.bits = bits;
  return tx.send(decoded, 0);
}

void printTxOk(const String &protocolText, const String &bitsText)
{
  Serial.print("TX_OK ");
  Serial.print(protocolText);
  Serial.print(" ");
  Serial.println(bitsText);
}

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

  bool ok = false;
  if (protocolText == "NEC")
  {
    ok = sendBits(esp32irpk::IRProtocolID::NEC, 32, bits);
  }
  else if (protocolText == "SONY12")
  {
    ok = sendBits(esp32irpk::IRProtocolID::SONY12, 12, bits);
  }
  else if (protocolText == "SONY15")
  {
    ok = sendBits(esp32irpk::IRProtocolID::SONY15, 15, bits);
  }
  else if (protocolText == "SONY20")
  {
    ok = sendBits(esp32irpk::IRProtocolID::SONY20, 20, bits);
  }
  else if (protocolText == "SAMSUNG32")
  {
    ok = sendBits(esp32irpk::IRProtocolID::SAMSUNG32, 32, bits);
  }
  else if (protocolText == "SAMSUNG36")
  {
    ok = sendBits(esp32irpk::IRProtocolID::SAMSUNG36, 36, bits);
  }
  else if (protocolText == "JVC24")
  {
    ok = sendBits(esp32irpk::IRProtocolID::JVC24, 24, bits);
  }
  else if (protocolText == "JVC32")
  {
    ok = sendBits(esp32irpk::IRProtocolID::JVC32, 32, bits);
  }
  else if (protocolText == "AEHA")
  {
    ok = sendBits(esp32irpk::IRProtocolID::AEHA, 48, bits);
  }
  else if (protocolText == "PANASONIC40")
  {
    ok = sendBits(esp32irpk::IRProtocolID::PANASONIC40, 40, bits);
  }
  else if (protocolText == "PANASONIC48")
  {
    ok = sendBits(esp32irpk::IRProtocolID::PANASONIC48, 48, bits);
  }
  else
  {
    Serial.println("TX_ERROR unsupported_protocol");
    return;
  }

  if (!ok)
  {
    Serial.println("TX_ERROR send_failed");
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
