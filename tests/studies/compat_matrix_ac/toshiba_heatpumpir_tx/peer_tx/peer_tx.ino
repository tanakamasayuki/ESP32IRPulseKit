// Compat-AC second reference (Toshiba) -- TX side (HeatpumpIR).
//
// Drives the IR LED with HeatpumpIR's ToshibaHeatpumpIR over an LEDC carrier
// (IRSenderESP32) -- independent of both ESP32IRPulseKit and IRremoteESP8266. The
// ESP32IRPulseKit primary decodes it and the harness checks our decoded fields
// against the known state (semantic agreement, not byte identity).
//
// HeatpumpIR stores the state bit-reversed; on the wire it is the standard
// MSB-first TOSHIBA_AC frame. Its mode constants reduce to wire mode codes
// (cool=1, dry=2, heat=3, auto=0; no FAN mode) and its FAN1..FAN5 to wire fan
// fields 2..6 (our MIN..MAX); auto=0. No swing (Toshiba swing is a separate
// short message).
//
// Command: SEND_AC mode=<AUTO|COOL|DRY|HEAT> fan=<AUTO|MIN|LOW|MED|HIGH|MAX> temp=<C> power=<0|1>
#include <Arduino.h>
#include <HeatpumpIR.h>
#include <ToshibaHeatpumpIR.h>

#ifndef IR_TX_GPIO
#define IR_TX_GPIO "4"
#endif

const int kIrTxGpio = atoi(IR_TX_GPIO);
const uint8_t kLedcChannel = 0;

IRSenderESP32 irSender(kIrTxGpio, kLedcChannel);
ToshibaHeatpumpIR heatpump;

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

// HeatpumpIR's Toshiba has no FAN operating mode (auto/cool/dry/heat only).
bool mapMode(const String &name, uint8_t &mode)
{
  if (name == "AUTO") { mode = MODE_AUTO; return true; }
  if (name == "COOL") { mode = MODE_COOL; return true; }
  if (name == "DRY")  { mode = MODE_DRY;  return true; }
  if (name == "HEAT") { mode = MODE_HEAT; return true; }
  return false;
}

bool mapFan(const String &name, uint8_t &fan)
{
  if (name == "AUTO") { fan = FAN_AUTO; return true; }
  if (name == "MIN")  { fan = FAN_1;    return true; }
  if (name == "LOW")  { fan = FAN_2;    return true; }
  if (name == "MED")  { fan = FAN_3;    return true; }
  if (name == "HIGH") { fan = FAN_4;    return true; }
  if (name == "MAX")  { fan = FAN_5;    return true; }
  return false;
}

void sendReady()
{
  Serial.print("TX_READY impl=HeatpumpIR gpio=");
  Serial.print(kIrTxGpio);
  Serial.println(" inverted=0");
}

void handleSendAc(const String &line)
{
  uint8_t mode = 0;
  uint8_t fan = 0;
  if (!mapMode(fieldValue(line, "mode"), mode))
  {
    Serial.println("TX_ERROR invalid_mode");
    return;
  }
  if (!mapFan(fieldValue(line, "fan"), fan))
  {
    Serial.println("TX_ERROR invalid_fan");
    return;
  }
  const uint8_t temp = (uint8_t)fieldValue(line, "temp").toInt();
  const uint8_t power = fieldValue(line, "power").toInt() != 0 ? POWER_ON : POWER_OFF;

  heatpump.send(irSender, power, mode, fan, temp, VDIR_AUTO, HDIR_AUTO);
  Serial.println("TX_OK_AC vendor=TOSHIBA impl=HeatpumpIR");
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
  sendReady();
}

void loop()
{
  String line;
  if (readLine(line))
    handleCommand(line);
  delay(1);
}
