// Compat-AC second reference (Fujitsu) -- TX side (HeatpumpIR).
//
// Drives the IR LED with HeatpumpIR's FujitsuHeatpumpIR over an LEDC carrier
// (IRSenderESP32) -- a transmit path independent of both ESP32IRPulseKit and
// IRremoteESP8266. The ESP32IRPulseKit primary decodes it and the harness checks
// our decoded fields against the known state (semantic agreement, not byte
// identity: HeatpumpIR fills byte 14 differently from IRremoteESP8266).
//
// HeatpumpIR's Fujitsu fan constants are inverted vs the wire codes
// (FAN1=0x04=quiet .. FAN4=0x01=high), so this peer maps our speed tokens to the
// HeatpumpIR FAN_x that produces the intended wire code. HeatpumpIR offers only
// SWING vs MANUAL per axis (no fixed positions), which is enough for OFF /
// VERTICAL / HORIZONTAL / BOTH.
//
// Command: SEND_AC mode=<AUTO|COOL|DRY|FAN|HEAT> fan=<AUTO|HIGH|MED|LOW|QUIET> temp=<C> swing=<OFF|VERT|HORIZ|BOTH> power=<0|1>
#include <Arduino.h>
#include <HeatpumpIR.h>
#include <FujitsuHeatpumpIR.h>

#ifndef IR_TX_GPIO
#define IR_TX_GPIO "4"
#endif

const int kIrTxGpio = atoi(IR_TX_GPIO);
const uint8_t kLedcChannel = 0; // ignored on core 3.x (LEDC binds to the pin)

IRSenderESP32 irSender(kIrTxGpio, kLedcChannel);
FujitsuHeatpumpIR heatpump;

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

// HeatpumpIR Fujitsu fan codes are inverted vs the wire codes: FAN_1 -> wire 4
// (quiet), FAN_4 -> wire 1 (high). Pick the FAN_x that yields the intended wire
// code, which our decoder reports as the matching esp32irpk::ac::Fujitsu::Fan.
bool mapFan(const String &name, uint8_t &fan)
{
  if (name == "AUTO")  { fan = FAN_AUTO; return true; }
  if (name == "HIGH")  { fan = FAN_4;    return true; } // wire 1 = HIGH_SPEED
  if (name == "MED")   { fan = FAN_3;    return true; } // wire 2 = MED_SPEED
  if (name == "LOW")   { fan = FAN_2;    return true; } // wire 3 = LOW_SPEED
  if (name == "QUIET") { fan = FAN_1;    return true; } // wire 4 = QUIET
  return false;
}

// HeatpumpIR maps swing per axis to SWING (on) vs MANUAL (off). VDIR_SWING sets
// byte-10 bit 4 (our VERTICAL), HDIR_SWING sets bit 5 (our HORIZONTAL).
bool mapSwing(const String &name, uint8_t &swingV, uint8_t &swingH)
{
  if (name == "OFF")   { swingV = VDIR_AUTO;  swingH = HDIR_AUTO;  return true; }
  if (name == "VERT")  { swingV = VDIR_SWING; swingH = HDIR_AUTO;  return true; }
  if (name == "HORIZ") { swingV = VDIR_AUTO;  swingH = HDIR_SWING; return true; }
  if (name == "BOTH")  { swingV = VDIR_SWING; swingH = HDIR_SWING; return true; }
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
  uint8_t swingV = 0;
  uint8_t swingH = 0;
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
  if (!mapSwing(fieldValue(line, "swing"), swingV, swingH))
  {
    Serial.println("TX_ERROR invalid_swing");
    return;
  }
  const uint8_t temp = (uint8_t)fieldValue(line, "temp").toInt();
  const uint8_t power = fieldValue(line, "power").toInt() != 0 ? POWER_ON : POWER_OFF;

  heatpump.send(irSender, power, mode, fan, temp, swingV, swingH);

  Serial.println("TX_OK_AC vendor=FUJITSU impl=HeatpumpIR");
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
