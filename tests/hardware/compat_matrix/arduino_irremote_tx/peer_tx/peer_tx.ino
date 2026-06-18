// Compat peer TX: drives the IR LED with Arduino-IRremote (IRremote.hpp).
// The serial command protocol matches protocol_matrix's peer
// (SEND <PROTOCOL> <hex>, PING/READY) so the same pytest harness can compare
// how ESP32IRPulseKit RX decodes frames produced by an external library.
//
// Arduino-IRremote's modern send API is address/command oriented, so to send an
// arbitrary raw payload we use the deprecated MSB-first raw senders
// (send<Protocol>MSB). They emit a well-defined bitstream (the given value,
// MSB-first), which is exactly what a compatibility matrix wants to observe.
// Note: Arduino-IRremote does not support an inverted TX output, so
// IR_TX_INVERTED is reported but not applied here.
#include <Arduino.h>
#define DISABLE_CODE_FOR_RECEIVER  // send-only sketch; skip RX/timer code
#include <IRremote.hpp>

#ifndef IR_TX_GPIO
#define IR_TX_GPIO "4"
#endif

#ifndef IR_TX_INVERTED
#define IR_TX_INVERTED "0"
#endif

const int kIrTxGpio = atoi(IR_TX_GPIO);
const bool kIrTxInverted = atoi(IR_TX_INVERTED) != 0;

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
  Serial.print("TX_READY impl=Arduino-IRremote gpio=");
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

// Send a single frame per command (MSB-first raw value) so the RX side observes
// exactly one decode.
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
    IrSender.sendNECMSB((uint32_t)bits, 32);
  }
  else if (protocolText == "SONY12")
  {
    IrSender.sendSonyMSB((unsigned long)bits, 12);
  }
  else if (protocolText == "SONY15")
  {
    IrSender.sendSonyMSB((unsigned long)bits, 15);
  }
  else if (protocolText == "SONY20")
  {
    IrSender.sendSonyMSB((unsigned long)bits, 20);
  }
  else if (protocolText == "SAMSUNG32")
  {
    IrSender.sendSamsungMSB((unsigned long)bits, 32);
  }
  else if (protocolText == "SAMSUNG36")
  {
    IrSender.sendSamsungMSB((unsigned long)bits, 36);
  }
  else if (protocolText == "JVC")
  {
    IrSender.sendJVCMSB((unsigned long)bits, 16);
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
  IrSender.begin(kIrTxGpio);
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
