// Compat-AC field-map calibration (TCL) -- TX side.
//
// Drives the IR LED with IRremoteESP8266's IRTcl112Ac (14-byte TCL112AC). Builds a
// known A/C state from a key=value command, transmits it, and echoes the exact 14
// bytes it sent so the harness can compare them against what our (ESP32IRPulseKit)
// primary RX decodes. getRaw() returns the raw[] array in transmission order (byte 0
// = 0x23 signature first), matching esp32irpk::ac::Tcl's byte order.
//
// setFan is called AFTER setMode so the loop's fan wins over setMode(FAN)'s
// fan=high side effect (which our Frame mirrors). Peer name "tx" reuses
// TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3.
//
// Command: SEND_AC mode=<AUTO|COOL|DRY|HEAT|FAN> fan=<AUTO|MIN|LOW|MED|HIGH> temp=<C> power=<0|1>
#include <Arduino.h>
#include <ir_Tcl.h>

#ifndef IR_TX_GPIO
#define IR_TX_GPIO "4"
#endif

#ifndef IR_TX_INVERTED
#define IR_TX_INVERTED "0"
#endif

const int kIrTxGpio = atoi(IR_TX_GPIO);
const bool kIrTxInverted = atoi(IR_TX_INVERTED) != 0;

IRTcl112Ac ac(kIrTxGpio, kIrTxInverted);

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
  if (name == "AUTO") { mode = kTcl112AcAuto; return true; }
  if (name == "COOL") { mode = kTcl112AcCool; return true; }
  if (name == "DRY")  { mode = kTcl112AcDry;  return true; }
  if (name == "HEAT") { mode = kTcl112AcHeat; return true; }
  if (name == "FAN")  { mode = kTcl112AcFan;  return true; }
  return false;
}

bool mapFan(const String &name, uint8_t &fan)
{
  if (name == "AUTO") { fan = kTcl112AcFanAuto; return true; }
  if (name == "MIN")  { fan = kTcl112AcFanMin;  return true; }
  if (name == "LOW")  { fan = kTcl112AcFanLow;  return true; }
  if (name == "MED")  { fan = kTcl112AcFanMed;  return true; }
  if (name == "HIGH") { fan = kTcl112AcFanHigh; return true; }
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
  ac.setMode(mode);           // may force fan=high for FAN mode
  ac.setFan(fan);             // overrides so the requested fan wins
  ac.setTemp(fieldValue(line, "temp").toFloat());
  ac.setPower(fieldValue(line, "power").toInt() != 0);
  ac.send();

  Serial.print("TX_OK_AC vendor=TCL bytes=");
  printStateHex(ac.getRaw(), kTcl112AcStateLength);
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
