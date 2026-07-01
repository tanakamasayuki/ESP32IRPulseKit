// Compat-AC field-map calibration (Carrier) -- TX side.
//
// Drives the IR LED with IRremoteESP8266's IRCarrierAc64 (CARRIER_AC64, 8-byte).
// Builds a known A/C state from a key=value command, transmits it, and echoes the
// exact 8 bytes it sent so the harness can compare them against what our
// (ESP32IRPulseKit) primary RX decodes. The bytes are echoed LSB-first (byte 0 =
// 0x84 signature ... byte 7), matching esp32irpk::ac::Carrier's wire byte order.
// Peer name "tx" reuses TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3.
//
// Command: SEND_AC mode=<HEAT|COOL|FAN> fan=<AUTO|LOW|MED|HIGH> temp=<C> power=<0|1>
#include <Arduino.h>
#include <ir_Carrier.h>

#ifndef IR_TX_GPIO
#define IR_TX_GPIO "4"
#endif

#ifndef IR_TX_INVERTED
#define IR_TX_INVERTED "0"
#endif

const int kIrTxGpio = atoi(IR_TX_GPIO);
const bool kIrTxInverted = atoi(IR_TX_INVERTED) != 0;

IRCarrierAc64 ac(kIrTxGpio, kIrTxInverted);

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
  if (name == "HEAT") { mode = kCarrierAc64Heat; return true; }
  if (name == "COOL") { mode = kCarrierAc64Cool; return true; }
  if (name == "FAN")  { mode = kCarrierAc64Fan;  return true; }
  return false;
}

bool mapFan(const String &name, uint8_t &fan)
{
  if (name == "AUTO") { fan = kCarrierAc64FanAuto;   return true; }
  if (name == "LOW")  { fan = kCarrierAc64FanLow;    return true; }
  if (name == "MED")  { fan = kCarrierAc64FanMedium; return true; }
  if (name == "HIGH") { fan = kCarrierAc64FanHigh;   return true; }
  return false;
}

// Print the 64-bit state LSB-first (byte 0 = 0x84 signature ... byte 7), matching
// esp32irpk::ac::Carrier's wire byte order.
void printStateHex(uint64_t state)
{
  for (int i = 0; i < 8; ++i)
  {
    uint8_t b = (uint8_t)((state >> (i * 8)) & 0xFF);
    if (b < 0x10)
      Serial.print('0');
    Serial.print(b, HEX);
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
  ac.setSwingV(false);
  ac.send();

  Serial.print("TX_OK_AC vendor=CARRIER bytes=");
  printStateHex(ac.getRaw());
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
