// Compat-AC field-map calibration (Sharp) -- TX side.
//
// Drives the IR LED with IRremoteESP8266's IRSharpAc (standard 13-byte SHARP_AC,
// default A907 model). Builds a known A/C state from a key=value command, transmits
// it, and echoes the exact 13 bytes it sent so the harness can compare them against
// what our (ESP32IRPulseKit) primary RX decodes. Peer name "tx" reuses
// TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3.
//
// setPower is called LAST on purpose: each setter stamps the "Special" byte with
// which button it represents (setFan -> fan, setMode/setPower -> power). Ending with
// setPower leaves Special = power (0x00), matching what our encoder always emits.
//
// Command: SEND_AC mode=<AUTO|HEAT|COOL|DRY> fan=<AUTO|MIN|MED|HIGH|MAX> temp=<C> power=<0|1>
#include <Arduino.h>
#include <ir_Sharp.h>

#ifndef IR_TX_GPIO
#define IR_TX_GPIO "4"
#endif

#ifndef IR_TX_INVERTED
#define IR_TX_INVERTED "0"
#endif

const int kIrTxGpio = atoi(IR_TX_GPIO);
const bool kIrTxInverted = atoi(IR_TX_INVERTED) != 0;

// IRSharpAc(pin, inverted, use_modulation): use_modulation defaults true -- keep the
// 38kHz carrier on, else a TSOP receiver detects nothing.
IRSharpAc ac(kIrTxGpio, kIrTxInverted);

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
  if (name == "AUTO") { mode = kSharpAcAuto; return true; }
  if (name == "HEAT") { mode = kSharpAcHeat; return true; }
  if (name == "COOL") { mode = kSharpAcCool; return true; }
  if (name == "DRY")  { mode = kSharpAcDry;  return true; }
  return false;
}

bool mapFan(const String &name, uint8_t &fan)
{
  if (name == "AUTO") { fan = kSharpAcFanAuto; return true; }
  if (name == "MIN")  { fan = kSharpAcFanMin;  return true; }
  if (name == "MED")  { fan = kSharpAcFanMed;  return true; }
  if (name == "HIGH") { fan = kSharpAcFanHigh; return true; }
  if (name == "MAX")  { fan = kSharpAcFanMax;  return true; }
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
  // Order matters: setMode forces fan/temp in Auto/Dry, and each setter stamps the
  // Special byte. End with setPower so Special = power (0x00).
  ac.setMode(mode);
  ac.setTemp((uint8_t)fieldValue(line, "temp").toInt());
  ac.setFan(fan);
  ac.setPower(fieldValue(line, "power").toInt() != 0);
  ac.send();

  Serial.print("TX_OK_AC vendor=SHARP bytes=");
  printStateHex(ac.getRaw(), kSharpAcStateLength);
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
