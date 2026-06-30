// Compat-AC encoder verification (Samsung) -- TX side (ESP32IRPulseKit).
//
// Builds a known Samsung A/C state (standard 14-byte SAMSUNG_AC) with
// esp32irpk::ac::Samsung from the harness command, renders it to RAW with toRaw()
// (LSB-first, leading header + two 7-byte sections), and transmits it. Echoes the
// exact 14 bytes it sent (recovered via our own fromRaw) so the harness can compare
// against what the IRremoteESP8266 primary decodes. Peer name "tx" reuses
// TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3.
//
// Carrier defaults to phase-aligned (Samsung zero-space 436us < bit mark 586us).
//
// Commands:
//   SEND_AC mode=<AUTO|COOL|DRY|FAN|HEAT> fan=<AUTO|LOW|MED|HIGH|MAX> temp=<C> power=<0|1>
//   CARRIER <pa|hw>
#include <ESP32IRPulseKit.h>

#ifndef IR_TX_GPIO
#define IR_TX_GPIO "4"
#endif

#ifndef IR_TX_INVERTED
#define IR_TX_INVERTED "0"
#endif

#ifndef PULSEKIT_CARRIER
#define PULSEKIT_CARRIER 1
#endif

const int kIrTxGpio = atoi(IR_TX_GPIO);
const bool kIrTxInverted = atoi(IR_TX_INVERTED) != 0;

esp32irpk::IRSender tx(kIrTxGpio, kIrTxInverted);

bool g_phaseAligned = (PULSEKIT_CARRIER != 0);
bool g_begun = false;

using Frame = esp32irpk::ac::Samsung::Frame;
using Mode = esp32irpk::ac::Samsung::Mode;
using Fan = esp32irpk::ac::Samsung::Fan;

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

bool mapMode(const String &name, Mode &mode)
{
  if (name == "AUTO") { mode = Mode::AUTO; return true; }
  if (name == "COOL") { mode = Mode::COOL; return true; }
  if (name == "DRY")  { mode = Mode::DRY;  return true; }
  if (name == "FAN")  { mode = Mode::FAN;  return true; }
  if (name == "HEAT") { mode = Mode::HEAT; return true; }
  return false;
}

bool mapFan(const String &name, Fan &fan)
{
  if (name == "AUTO") { fan = Fan::AUTO;       return true; }
  if (name == "LOW")  { fan = Fan::LOW_SPEED;  return true; }
  if (name == "MED")  { fan = Fan::MED_SPEED;  return true; }
  if (name == "HIGH") { fan = Fan::HIGH_SPEED; return true; }
  if (name == "MAX")  { fan = Fan::MAX_SPEED;  return true; }
  return false;
}

void printStateHex(const uint8_t *bytes, uint16_t nbytes)
{
  for (uint16_t i = 0; i < nbytes; ++i)
  {
    if (bytes[i] < 0x10)
      Serial.print('0');
    Serial.print(bytes[i], HEX);
  }
}

void sendReady()
{
  Serial.print("TX_READY impl=ESP32IRPulseKit gpio=");
  Serial.print(kIrTxGpio);
  Serial.print(" inverted=");
  Serial.print(kIrTxInverted ? 1 : 0);
  Serial.print(" carrier=");
  Serial.println(g_phaseAligned ? "pa" : "hw");
}

bool ensureMode(bool phaseAligned)
{
  if (g_begun && g_phaseAligned == phaseAligned)
    return true;
  if (g_begun)
  {
    tx.end();
    g_begun = false;
  }
  tx.setPhaseAlignedCarrier(phaseAligned);
  if (!tx.begin())
    return false;
  g_begun = true;
  g_phaseAligned = phaseAligned;
  return true;
}

void handleSendAc(const String &line)
{
  Mode mode;
  Fan fan;
  if (!mapMode(fieldValue(line, "mode"), mode) || !mapFan(fieldValue(line, "fan"), fan))
  {
    Serial.println("TX_ERROR invalid_args");
    return;
  }
  Frame f{};
  f.setMode(mode);
  f.setFan(fan);
  f.setTemperatureC((uint8_t)fieldValue(line, "temp").toInt());
  f.setPower(fieldValue(line, "power").toInt() != 0);

  uint16_t ticks[Frame::kMaxTicks];
  esp32irpk::IRRawTickBuffer buf{ticks, Frame::kMaxTicks, 0};
  if (!f.toRaw(buf))
  {
    Serial.println("TX_ERROR encode_failed");
    return;
  }
  if (!tx.send(esp32irpk::IRRawTickView{buf.ticks, buf.len}))
  {
    Serial.println("TX_ERROR send_failed");
    return;
  }
  Frame canonical{};
  if (!Frame::fromRaw(esp32irpk::IRRawTickView{buf.ticks, buf.len}, canonical))
  {
    Serial.println("TX_ERROR self_decode_failed");
    return;
  }
  Serial.print("TX_OK_AC vendor=SAMSUNG bytes=");
  printStateHex(canonical.bytes, canonical.byte_length);
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
  if (line == "CARRIER pa" || line == "CARRIER hw")
  {
    if (!ensureMode(line.endsWith("pa")))
    {
      Serial.println("TX_ERROR begin_failed");
      return;
    }
    Serial.print("CARRIER_OK mode=");
    Serial.println(g_phaseAligned ? "pa" : "hw");
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
  if (!ensureMode(g_phaseAligned))
  {
    Serial.println("TX_ERROR begin_failed");
    return;
  }
  sendReady();
}

void loop()
{
  String line;
  if (readLine(line))
    handleCommand(line);
  delay(1);
}
