// Compat-AC self baseline -- TX side.
//
// Drives the IR LED with IRremoteESP8266's IRPanasonicAc. The harness sends a
// known A/C state as a key=value command; this builds that state, transmits it,
// and echoes back the exact 27 bytes it sent so the harness can compare them
// against what the (also-IRremoteESP8266) primary RX decodes. The peer name is
// "tx" so it reuses TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3, matching compat_matrix.
//
// Command: SEND_AC mode=<AUTO|COOL|HEAT|DRY> fan=<AUTO|LOW|MED|HIGH> temp=<C> power=<0|1>
#include <Arduino.h>
#include <ir_Panasonic.h>

#ifndef IR_TX_GPIO
#define IR_TX_GPIO "4"
#endif

#ifndef IR_TX_INVERTED
#define IR_TX_INVERTED "0"
#endif

const int kIrTxGpio = atoi(IR_TX_GPIO);
const bool kIrTxInverted = atoi(IR_TX_INVERTED) != 0;

IRPanasonicAc ac(kIrTxGpio, kIrTxInverted);

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

// Pull the value for key= out of a "k1=v1 k2=v2 ..." line. Returns "" if absent.
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
  if (name == "AUTO") { mode = kPanasonicAcAuto; return true; }
  if (name == "COOL") { mode = kPanasonicAcCool; return true; }
  if (name == "HEAT") { mode = kPanasonicAcHeat; return true; }
  if (name == "DRY")  { mode = kPanasonicAcDry;  return true; }
  return false;
}

bool mapFan(const String &name, uint8_t &fan)
{
  if (name == "AUTO") { fan = kPanasonicAcFanAuto; return true; }
  if (name == "LOW")  { fan = kPanasonicAcFanLow;  return true; }
  if (name == "MED")  { fan = kPanasonicAcFanMed;  return true; }
  if (name == "HIGH") { fan = kPanasonicAcFanHigh; return true; }
  return false;
}

// Optional model= field; absent defaults to JKE (the known-good base layout).
bool mapModel(const String &name, panasonic_ac_remote_model_t &model)
{
  if (name == "" || name == "JKE") { model = kPanasonicJke; return true; }
  if (name == "DKE") { model = kPanasonicDke; return true; }
  if (name == "NKE") { model = kPanasonicNke; return true; }
  if (name == "LKE") { model = kPanasonicLke; return true; }
  if (name == "RKR") { model = kPanasonicRkr; return true; }
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
  panasonic_ac_remote_model_t model;
  if (!mapModel(fieldValue(line, "model"), model))
  {
    Serial.println("TX_ERROR invalid_model");
    return;
  }
  const int temp = fieldValue(line, "temp").toInt();
  const bool power = fieldValue(line, "power").toInt() != 0;

  // setModel first: it stamps the model marker bytes (and clears the power bit),
  // then the logical setters apply on top.
  ac.setModel(model);
  ac.setPower(power);
  ac.setMode(mode);
  ac.setFan(fan);
  ac.setTemp((uint8_t)temp);
  ac.send();

  Serial.print("TX_OK_AC vendor=PANASONIC model=");
  Serial.print(fieldValue(line, "model").length() ? fieldValue(line, "model") : "JKE");
  Serial.print(" bytes=");
  printStateHex(ac.getRaw(), kPanasonicAcStateLength);
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
