// Compat-AC field-map calibration (Samsung) -- TX side.
//
// Drives the IR LED with IRremoteESP8266's IRSamsungAc (standard 14-byte
// SAMSUNG_AC) state builder. Builds a known A/C state from a key=value command and
// transmits the standard 14-byte frame via IRsend::sendSamsungAC, then echoes the
// exact 14 bytes it sent so the harness can compare them against what our
// (ESP32IRPulseKit) primary RX decodes. Peer name "tx" reuses
// TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3.
//
// We send via the low-level IRsend (not IRSamsungAc::send) on purpose: the class's
// send() emits a 21-byte EXTENDED message whenever the power state changes, which
// is a different frame. sendSamsungAC(getRaw(), 14) always emits the standard
// 14-byte message that matches our decoder.
//
// Command: SEND_AC mode=<AUTO|COOL|DRY|FAN|HEAT> fan=<AUTO|LOW|MED|HIGH|MAX> temp=<C> power=<0|1>
#include <Arduino.h>
#include <IRsend.h>
#include <ir_Samsung.h>

#ifndef IR_TX_GPIO
#define IR_TX_GPIO "4"
#endif

#ifndef IR_TX_INVERTED
#define IR_TX_INVERTED "0"
#endif

const int kIrTxGpio = atoi(IR_TX_GPIO);
const bool kIrTxInverted = atoi(IR_TX_INVERTED) != 0;

// IRsend(pin, inverted, use_modulation): use_modulation defaults true -- keep the
// 38kHz carrier on, else a TSOP receiver detects nothing.
IRsend irsend(kIrTxGpio, kIrTxInverted);
IRSamsungAc ac(0); // state builder only; the pin is unused (we drive irsend).

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
  if (name == "AUTO") { mode = kSamsungAcAuto; return true; }
  if (name == "COOL") { mode = kSamsungAcCool; return true; }
  if (name == "DRY")  { mode = kSamsungAcDry;  return true; }
  if (name == "FAN")  { mode = kSamsungAcFan;  return true; }
  if (name == "HEAT") { mode = kSamsungAcHeat; return true; }
  return false;
}

// IRSamsungAc::setFan takes the wire code directly. In Auto mode only the special
// auto2 fan is valid, so pair AUTO mode with AUTO fan in the test cases.
bool mapFan(const String &name, uint8_t &fan)
{
  if (name == "AUTO") { fan = kSamsungAcFanAuto; return true; }
  if (name == "LOW")  { fan = kSamsungAcFanLow;  return true; }
  if (name == "MED")  { fan = kSamsungAcFanMed;  return true; }
  if (name == "HIGH") { fan = kSamsungAcFanHigh; return true; }
  if (name == "MAX")  { fan = kSamsungAcFanTurbo; return true; }
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
  // Order matters: setMode adjusts the fan field in/out of Auto, so set it first.
  ac.setMode(mode);
  ac.setFan(fan);
  ac.setTemp((uint8_t)fieldValue(line, "temp").toInt());
  ac.setPower(fieldValue(line, "power").toInt() != 0);
  irsend.sendSamsungAC(ac.getRaw(), kSamsungAcStateLength);

  Serial.print("TX_OK_AC vendor=SAMSUNG bytes=");
  printStateHex(ac.getRaw(), kSamsungAcStateLength);
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
  irsend.begin();
  sendReady();
}

void loop()
{
  String line;
  if (readLine(line))
    handleCommand(line);
  delay(1);
}
