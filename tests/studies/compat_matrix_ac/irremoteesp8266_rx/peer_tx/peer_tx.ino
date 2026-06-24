// Compat-AC encoder verification -- TX side (ESP32IRPulseKit).
//
// Builds a known Panasonic A/C state with esp32irpk::ac::Panasonic from the
// harness command, renders it to RAW with toRaw(), and transmits it. AC frames
// are long, so the hardware (free-running) carrier is used -- the phase-aligned
// carrier would explode the symbol count (see example 07 / SPEC 11). The peer
// echoes the exact 27 bytes it sent (recovered via our own fromRaw, which the
// irremoteesp8266_tx variant verified byte-for-byte) so the harness can compare
// them against what the IRremoteESP8266 primary decodes.
//
// Command: SEND_AC mode=<AUTO|COOL|HEAT|DRY> fan=<AUTO|LOW|MED|HIGH> temp=<C> power=<0|1>
#include <ESP32IRPulseKit.h>

#ifndef IR_TX_GPIO
#define IR_TX_GPIO "4"
#endif

#ifndef IR_TX_INVERTED
#define IR_TX_INVERTED "0"
#endif

const int kIrTxGpio = atoi(IR_TX_GPIO);
const bool kIrTxInverted = atoi(IR_TX_INVERTED) != 0;

esp32irpk::IRSender tx(kIrTxGpio, kIrTxInverted);

using Frame = esp32irpk::ac::Panasonic::Frame;
using Mode = esp32irpk::ac::Panasonic::Mode;
using Fan = esp32irpk::ac::Panasonic::Fan;

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
  if (name == "HEAT") { mode = Mode::HEAT; return true; }
  if (name == "DRY")  { mode = Mode::DRY;  return true; }
  return false;
}

bool mapFan(const String &name, Fan &fan)
{
  if (name == "AUTO") { fan = Fan::AUTO;       return true; }
  if (name == "LOW")  { fan = Fan::LOW_SPEED;  return true; }
  if (name == "MED")  { fan = Fan::MED_SPEED;  return true; }
  if (name == "HIGH") { fan = Fan::HIGH_SPEED; return true; }
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
  Serial.println(kIrTxInverted ? 1 : 0);
}

void handleSendAc(const String &line)
{
  Mode mode;
  Fan fan;
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

  Frame f{};
  f.setPower(power);
  f.setMode(mode);
  f.setFan(fan);
  f.setTemperatureC((uint8_t)temp);

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

  // Recover the exact transmitted bytes (signature/preamble/checksum stamped by
  // toRaw) by decoding our own rendered burst.
  Frame canonical{};
  if (!Frame::fromRaw(esp32irpk::IRRawTickView{buf.ticks, buf.len}, canonical))
  {
    Serial.println("TX_ERROR self_decode_failed");
    return;
  }

  Serial.print("TX_OK_AC vendor=PANASONIC bytes=");
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
  Serial.print("TX_ERROR unknown_command ");
  Serial.println(line);
}
} // namespace

void setup()
{
  Serial.begin(115200);
  delay(5000);
  tx.setPhaseAlignedCarrier(false); // hardware carrier: long AC frames
  if (!tx.begin())
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
