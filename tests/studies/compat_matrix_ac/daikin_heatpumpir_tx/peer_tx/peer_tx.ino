// Compat-AC second reference (Daikin) -- TX side (HeatpumpIR).
//
// Drives the IR LED with HeatpumpIR's DaikinHeatpumpIR over an LEDC carrier
// (IRSenderESP32) -- a transmit path independent of both ESP32IRPulseKit and
// IRremoteESP8266. The ESP32IRPulseKit primary decodes it and the harness checks
// our decoded fields against the known state (semantic agreement, not byte
// identity).
//
// HeatpumpIR's Daikin emits the classic 35-byte, 3-section ARC433 frame. Its mode
// constants already sit in byte-21 bits 4-6 (heat=0x40, cool=0x30, ...) and its
// fan constants in byte-24's high nibble (FAN1=0x30 .. FAN5=0x70, AUTO=0xA0), so
// our decoder reads them directly. HeatpumpIR's Daikin does NOT drive swing (it is
// fixed off) and has no quiet step.
//
// Command: SEND_AC mode=<AUTO|COOL|DRY|FAN|HEAT> fan=<AUTO|MIN|LOW|MED|HIGH|MAX> temp=<C> power=<0|1>
#include <Arduino.h>
#include <HeatpumpIR.h>
#include <DaikinHeatpumpIR.h>

#ifndef IR_TX_GPIO
#define IR_TX_GPIO "4"
#endif

const int kIrTxGpio = atoi(IR_TX_GPIO);
const uint8_t kLedcChannel = 0; // ignored on core 3.x (LEDC binds to the pin)

IRSenderESP32 irSender(kIrTxGpio, kLedcChannel);
DaikinHeatpumpIR heatpump;

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
  if (name == "AUTO") { mode = MODE_AUTO; return true; }
  if (name == "COOL") { mode = MODE_COOL; return true; }
  if (name == "DRY")  { mode = MODE_DRY;  return true; }
  if (name == "FAN")  { mode = MODE_FAN;  return true; }
  if (name == "HEAT") { mode = MODE_HEAT; return true; }
  return false;
}

// HeatpumpIR Daikin fan FAN_1..FAN_5 map to wire nibble 3..7 (our MIN..MAX);
// FAN_AUTO -> 0xA. No quiet step.
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

  // Daikin's encoder ignores the swing args; pass AUTO.
  heatpump.send(irSender, power, mode, fan, temp, VDIR_AUTO, HDIR_AUTO);

  Serial.println("TX_OK_AC vendor=DAIKIN impl=HeatpumpIR");
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
