// Compat-AC field-map calibration (Midea) -- TX side.
//
// Drives the IR LED with IRremoteESP8266's IRMideaAC (48-bit / 6-byte). Builds a
// known A/C state from a key=value command, transmits it (data + inverted copy),
// and echoes the exact 6 bytes it sent so the harness can compare them against
// what our (ESP32IRPulseKit) primary RX decodes. The echoed bytes are in wire /
// transmission order (MSB first: byte 0 = 0xA1 header, byte 5 = checksum), which
// is the reverse of IRMideaAC's remote_state union byte order. Peer name "tx"
// reuses TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3.
//
// Command: SEND_AC mode=<AUTO|COOL|DRY|FAN|HEAT> fan=<AUTO|LOW|MED|HIGH> temp=<C> power=<0|1>
#include <Arduino.h>
#include <ir_Midea.h>

#ifndef IR_TX_GPIO
#define IR_TX_GPIO "4"
#endif

#ifndef IR_TX_INVERTED
#define IR_TX_INVERTED "0"
#endif

const int kIrTxGpio = atoi(IR_TX_GPIO);
const bool kIrTxInverted = atoi(IR_TX_INVERTED) != 0;

IRMideaAC ac(kIrTxGpio, kIrTxInverted);

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
  if (name == "COOL") { mode = kMideaACCool; return true; }
  if (name == "DRY")  { mode = kMideaACDry;  return true; }
  if (name == "AUTO") { mode = kMideaACAuto; return true; }
  if (name == "HEAT") { mode = kMideaACHeat; return true; }
  if (name == "FAN")  { mode = kMideaACFan;  return true; }
  return false;
}

bool mapFan(const String &name, uint8_t &fan)
{
  if (name == "AUTO") { fan = kMideaACFanAuto; return true; }
  if (name == "LOW")  { fan = kMideaACFanLow;  return true; }
  if (name == "MED")  { fan = kMideaACFanMed;  return true; }
  if (name == "HIGH") { fan = kMideaACFanHigh; return true; }
  return false;
}

// Print the 48-bit state MSB-first (byte 0 = 0xA1 header ... byte 5 = checksum),
// matching esp32irpk::ac::Midea's wire byte order.
void printStateHex(uint64_t state)
{
  for (int i = 5; i >= 0; --i)
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
  ac.setUseCelsius(true);
  ac.setPower(fieldValue(line, "power").toInt() != 0);
  ac.setMode(mode);
  ac.setTemp((uint8_t)fieldValue(line, "temp").toInt(), true); // Celsius
  ac.setFan(fan);
  ac.send();

  Serial.print("TX_OK_AC vendor=MIDEA bytes=");
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
