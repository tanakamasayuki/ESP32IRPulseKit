// Compat-AC encoder verification (Fujitsu) -- TX side (ESP32IRPulseKit).
//
// Builds a known Fujitsu A/C state with esp32irpk::ac::Fujitsu from the harness
// command, renders it to RAW with toRaw() (a 16-byte long frame, or a 7-byte
// short frame when power=0), and transmits it. The peer echoes the exact bytes it
// sent (recovered via our own fromRaw, which the fujitsu_irremoteesp8266_tx
// variant verified byte-for-byte) so the harness can compare them against what
// the IRremoteESP8266 primary decodes. The peer name is "tx" so it reuses
// TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3, matching compat_matrix.
//
// Carrier mode is selectable (study_carrier_ab.py). Like Gree/Mitsubishi,
// Fujitsu's zero-space (390us) is shorter than its bit mark (448us), so the
// phase-aligned carrier is the reliable choice and the build-time default here is
// phase-aligned (PULSEKIT_CARRIER 1).
//
// Commands:
//   SEND_AC mode=<AUTO|COOL|DRY|FAN|HEAT> fan=<AUTO|HIGH|MED|LOW|QUIET> temp=<C> swing=<OFF|VERT|HORIZ|BOTH> power=<0|1>
//   CARRIER <pa|hw>
#include <ESP32IRPulseKit.h>

#ifndef IR_TX_GPIO
#define IR_TX_GPIO "4"
#endif

#ifndef IR_TX_INVERTED
#define IR_TX_INVERTED "0"
#endif

// 0 = free-running hardware carrier, 1 = phase-aligned carrier (Fujitsu default).
#ifndef PULSEKIT_CARRIER
#define PULSEKIT_CARRIER 1
#endif

const int kIrTxGpio = atoi(IR_TX_GPIO);
const bool kIrTxInverted = atoi(IR_TX_INVERTED) != 0;

esp32irpk::IRSender tx(kIrTxGpio, kIrTxInverted);

bool g_phaseAligned = (PULSEKIT_CARRIER != 0);
bool g_begun = false;

using Frame = esp32irpk::ac::Fujitsu::Frame;
using Mode = esp32irpk::ac::Fujitsu::Mode;
using Fan = esp32irpk::ac::Fujitsu::Fan;
using Swing = esp32irpk::ac::Fujitsu::Swing;

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
  if (name == "AUTO")  { fan = Fan::AUTO;       return true; }
  if (name == "HIGH")  { fan = Fan::HIGH_SPEED; return true; }
  if (name == "MED")   { fan = Fan::MED_SPEED;  return true; }
  if (name == "LOW")   { fan = Fan::LOW_SPEED;  return true; }
  if (name == "QUIET") { fan = Fan::QUIET;      return true; }
  return false;
}

bool mapSwing(const String &name, Swing &swing)
{
  if (name == "OFF")   { swing = Swing::OFF;        return true; }
  if (name == "VERT")  { swing = Swing::VERTICAL;   return true; }
  if (name == "HORIZ") { swing = Swing::HORIZONTAL; return true; }
  if (name == "BOTH")  { swing = Swing::BOTH;       return true; }
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

// (Re)open the sender in the requested carrier mode. The TX channel layout is
// fixed at begin(), so a mode change must end() then begin() again.
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
  Swing swing;
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
  if (!mapSwing(fieldValue(line, "swing"), swing))
  {
    Serial.println("TX_ERROR invalid_swing");
    return;
  }
  const int temp = fieldValue(line, "temp").toInt();
  const bool power = fieldValue(line, "power").toInt() != 0;

  Frame f{};
  f.setPower(power);
  f.setMode(mode);
  f.setFan(fan);
  f.setSwing(swing);
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

  // Recover the exact transmitted bytes (constants/checksum stamped by toRaw) by
  // decoding our own rendered burst.
  Frame canonical{};
  if (!Frame::fromRaw(esp32irpk::IRRawTickView{buf.ticks, buf.len}, canonical))
  {
    Serial.println("TX_ERROR self_decode_failed");
    return;
  }

  Serial.print("TX_OK_AC vendor=FUJITSU bytes=");
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
    const bool pa = line.endsWith("pa");
    if (!ensureMode(pa))
    {
      Serial.println("TX_ERROR begin_failed");
      return;
    }
    Serial.print("CARRIER_OK mode=");
    Serial.println(pa ? "pa" : "hw");
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
  if (!ensureMode(g_phaseAligned)) // build-time default: PULSEKIT_CARRIER
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
