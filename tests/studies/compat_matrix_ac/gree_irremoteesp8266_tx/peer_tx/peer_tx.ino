// Compat-AC field-map calibration (Gree) -- TX side.
//
// Drives the IR LED with IRremoteESP8266's IRGreeAC. The harness sends a known
// A/C state as a key=value command; this builds that state, transmits it, and
// echoes back the exact 8 bytes it sent so the harness can compare them against
// what our (ESP32IRPulseKit) primary RX decodes. The peer name is "tx" so it
// reuses TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3, matching compat_matrix.
//
// Command: SEND_AC mode=<AUTO|COOL|HEAT|DRY|FAN> fan=<AUTO|MIN|MED|MAX> temp=<C> power=<0|1>
#include <Arduino.h>
#include <ir_Gree.h>

#ifndef IR_TX_GPIO
#define IR_TX_GPIO "4"
#endif

#ifndef IR_TX_INVERTED
#define IR_TX_INVERTED "0"
#endif

const int kIrTxGpio = atoi(IR_TX_GPIO);
const bool kIrTxInverted = atoi(IR_TX_INVERTED) != 0;

// YBOFB model keeps the ModelA bit (byte2 bit6) clear regardless of power, which
// matches esp32irpk::ac::Gree's single-model template (byte2 = 0x20). The YAW1F
// default sets ModelA on power-on, perturbing byte2 and the checksum.
IRGreeAC ac(kIrTxGpio, gree_ac_remote_model_t::YBOFB, kIrTxInverted);

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
  if (name == "AUTO") { mode = kGreeAuto; return true; }
  if (name == "COOL") { mode = kGreeCool; return true; }
  if (name == "HEAT") { mode = kGreeHeat; return true; }
  if (name == "DRY")  { mode = kGreeDry;  return true; }
  if (name == "FAN")  { mode = kGreeFan;  return true; }
  return false;
}

bool mapFan(const String &name, uint8_t &fan)
{
  if (name == "AUTO") { fan = kGreeFanAuto; return true; }
  if (name == "MIN")  { fan = kGreeFanMin;  return true; }
  if (name == "MED")  { fan = kGreeFanMed;  return true; }
  if (name == "MAX")  { fan = kGreeFanMax;  return true; }
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
  const int temp = fieldValue(line, "temp").toInt();
  const bool power = fieldValue(line, "power").toInt() != 0;

  ac.setPower(power);
  ac.setMode(mode);
  ac.setFan(fan);
  ac.setTemp((uint8_t)temp);
  ac.send();

  Serial.print("TX_OK_AC vendor=GREE bytes=");
  printStateHex(ac.getRaw(), kGreeStateLength);
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
