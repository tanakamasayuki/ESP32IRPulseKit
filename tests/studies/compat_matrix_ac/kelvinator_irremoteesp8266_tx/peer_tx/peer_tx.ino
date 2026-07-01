// Compat-AC field-map calibration (Kelvinator) -- TX side.
//
// Drives the IR LED with IRremoteESP8266's IRKelvinatorAC (standard 16-byte, two
// blocks). Builds a known A/C state from a key=value command, transmits it, and
// echoes the exact 16 bytes it sent so the harness can compare them against what
// our (ESP32IRPulseKit) primary RX decodes. Peer name "tx" reuses
// TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3.
//
// Command: SEND_AC mode=<AUTO|COOL|DRY|FAN|HEAT> fan=<AUTO|MIN|LOW|MED|HIGH|MAX> temp=<C> power=<0|1>
#include <Arduino.h>
#include <ir_Kelvinator.h>

#ifndef IR_TX_GPIO
#define IR_TX_GPIO "4"
#endif

#ifndef IR_TX_INVERTED
#define IR_TX_INVERTED "0"
#endif

const int kIrTxGpio = atoi(IR_TX_GPIO);
const bool kIrTxInverted = atoi(IR_TX_INVERTED) != 0;

IRKelvinatorAC ac(kIrTxGpio, kIrTxInverted);

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
  if (name == "AUTO") { mode = kKelvinatorAuto; return true; }
  if (name == "COOL") { mode = kKelvinatorCool; return true; }
  if (name == "DRY")  { mode = kKelvinatorDry;  return true; }
  if (name == "FAN")  { mode = kKelvinatorFan;  return true; }
  if (name == "HEAT") { mode = kKelvinatorHeat; return true; }
  return false;
}

bool mapFan(const String &name, uint8_t &fan)
{
  if (name == "AUTO") { fan = 0; return true; }
  if (name == "MIN")  { fan = 1; return true; }
  if (name == "LOW")  { fan = 2; return true; }
  if (name == "MED")  { fan = 3; return true; }
  if (name == "HIGH") { fan = 4; return true; }
  if (name == "MAX")  { fan = 5; return true; }
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
  ac.setMode(mode); // Auto/Dry force 25C
  ac.setTemp((uint8_t)fieldValue(line, "temp").toInt());
  ac.setFan(fan);
  ac.setPower(fieldValue(line, "power").toInt() != 0);
  ac.send();

  Serial.print("TX_OK_AC vendor=KELVINATOR bytes=");
  printStateHex(ac.getRaw(), kKelvinatorStateLength);
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
