// Compat-AC field-map calibration (Haier) -- TX side.
//
// Drives the IR LED with IRremoteESP8266's IRHaierAC (9-byte HAIER_AC). Builds a
// known A/C state from a key=value command, transmits it, and echoes the exact 9
// bytes it sent so the harness can compare them against what our (ESP32IRPulseKit)
// primary RX decodes. getRaw() returns the raw[] array in transmission order (byte
// 0 = 0xA5 prefix first), matching esp32irpk::ac::Haier's byte order.
//
// HAIER_AC is command-oriented and has no power bit; power is the On/Off command,
// so this sets the command LAST (after the state setters, whose own command side
// effects it overwrites) to keep the byte output deterministic. Peer name "tx"
// reuses TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3.
//
// Command: SEND_AC mode=<AUTO|COOL|DRY|HEAT|FAN> fan=<AUTO|LOW|MED|HIGH> temp=<C> power=<0|1>
#include <Arduino.h>
#include <ir_Haier.h>

#ifndef IR_TX_GPIO
#define IR_TX_GPIO "4"
#endif

#ifndef IR_TX_INVERTED
#define IR_TX_INVERTED "0"
#endif

const int kIrTxGpio = atoi(IR_TX_GPIO);
const bool kIrTxInverted = atoi(IR_TX_INVERTED) != 0;

IRHaierAC ac(kIrTxGpio, kIrTxInverted);

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

String fieldValue(const String &line, const String &key)
{
  String needle = key + "=";
  int start = line.indexOf(needle);
  if (start < 0)
    return "";
  start += needle.length();
  int end = line.indexOf(' ', start);
  if (end < 0)
    end = line.length();
  return line.substring(start, end);
}

bool mapMode(const String &name, uint8_t &mode)
{
  if (name == "AUTO") { mode = kHaierAcAuto; return true; }
  if (name == "COOL") { mode = kHaierAcCool; return true; }
  if (name == "DRY")  { mode = kHaierAcDry;  return true; }
  if (name == "HEAT") { mode = kHaierAcHeat; return true; }
  if (name == "FAN")  { mode = kHaierAcFan;  return true; }
  return false;
}

bool mapFan(const String &name, uint8_t &fan)
{
  if (name == "AUTO") { fan = kHaierAcFanAuto; return true; }
  if (name == "LOW")  { fan = kHaierAcFanLow;  return true; }
  if (name == "MED")  { fan = kHaierAcFanMed;  return true; }
  if (name == "HIGH") { fan = kHaierAcFanHigh; return true; }
  return false;
}

void printStateHex(const uint8_t *state, uint16_t nbytes)
{
  for (uint16_t i = 0; i < nbytes; ++i)
  {
    if (state[i] < 0x10)
      Serial.print('0');
    Serial.print(state[i], HEX);
  }
}

void sendReady()
{
  Serial.print("TX_READY impl=IRremoteESP8266 gpio=");
  Serial.print(kIrTxGpio);
  Serial.print(" inverted=");
  Serial.println(kIrTxInverted ? 1 : 0);
}

void handleSendAc(const String &line)
{
  uint8_t mode = 0;
  uint8_t fan = 0;
  if (!mapMode(fieldValue(line, "mode"), mode) || !mapFan(fieldValue(line, "fan"), fan))
  {
    Serial.println("TX_ERROR invalid_args");
    return;
  }
  ac.setMode(mode);
  ac.setTemp((uint8_t)fieldValue(line, "temp").toInt());
  ac.setFan(fan);
  // Set the On/Off command LAST so it wins over the state setters' commands.
  ac.setCommand(fieldValue(line, "power").toInt() != 0 ? kHaierAcCmdOn : kHaierAcCmdOff);
  ac.send();

  Serial.print("TX_OK_AC vendor=HAIER bytes=");
  printStateHex(ac.getRaw(), kHaierACStateLength);
  Serial.println();
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
  if (line.startsWith("SEND_AC"))
  {
    handleSendAc(line);
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
  ac.begin();
  sendReady();
}

void loop()
{
  String line;
  if (readLine(line))
    handleCommand(line);
  delay(1);
}
