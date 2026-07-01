// Compat-AC field-map calibration (Hitachi) -- TX side.
//
// Drives the IR LED with IRremoteESP8266's IRHitachiAc (28-byte HITACHI_AC). Builds
// a known A/C state from a key=value command, transmits it, and echoes the exact 28
// bytes it sent so the harness can compare them against what our (ESP32IRPulseKit)
// primary RX decodes. getRaw() returns the raw[] array in transmission order (byte
// 0 first), matching esp32irpk::ac::Hitachi's byte order. Peer name "tx" reuses
// TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3.
//
// Command: SEND_AC mode=<AUTO|HEAT|COOL|DRY|FAN> fan=<AUTO|LOW|MED|HIGH> temp=<C> power=<0|1>
#include <Arduino.h>
#include <ir_Hitachi.h>

#ifndef IR_TX_GPIO
#define IR_TX_GPIO "4"
#endif

#ifndef IR_TX_INVERTED
#define IR_TX_INVERTED "0"
#endif

const int kIrTxGpio = atoi(IR_TX_GPIO);
const bool kIrTxInverted = atoi(IR_TX_INVERTED) != 0;

IRHitachiAc ac(kIrTxGpio, kIrTxInverted);

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
  if (name == "AUTO") { mode = kHitachiAcAuto; return true; }
  if (name == "HEAT") { mode = kHitachiAcHeat; return true; }
  if (name == "COOL") { mode = kHitachiAcCool; return true; }
  if (name == "DRY")  { mode = kHitachiAcDry;  return true; }
  if (name == "FAN")  { mode = kHitachiAcFan;  return true; }
  return false;
}

bool mapFan(const String &name, uint8_t &fan)
{
  if (name == "AUTO") { fan = kHitachiAcFanAuto; return true; }
  if (name == "LOW")  { fan = kHitachiAcFanLow;  return true; }
  if (name == "MED")  { fan = kHitachiAcFanMed;  return true; }
  if (name == "HIGH") { fan = kHitachiAcFanHigh; return true; }
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
  ac.setPower(fieldValue(line, "power").toInt() != 0);
  ac.setMode(mode);
  ac.setTemp((uint8_t)fieldValue(line, "temp").toInt());
  ac.setFan(fan);
  ac.send();

  Serial.print("TX_OK_AC vendor=HITACHI bytes=");
  printStateHex(ac.getRaw(), kHitachiAcStateLength);
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
