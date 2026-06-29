// Compat-AC second reference (Fujitsu) -- RX side (ESP32IRPulseKit).
//
// The peer TX (peer_tx/) is HeatpumpIR's FujitsuHeatpumpIR over an LEDC carrier --
// a transmit path independent of both ESP32IRPulseKit and IRremoteESP8266. This
// primary captures the burst with our RAW path and decodes it with
// esp32irpk::ac::Fujitsu. The harness checks our decoded fields against the known
// sent state (semantic agreement; byte identity is not expected since HeatpumpIR
// fills different auxiliary bytes -- e.g. byte 14 -- than IRremoteESP8266).
//
// HeatpumpIR produces the same ARRAH2E-compatible 16-byte long frame (byte 5 =
// 0xFE) and a checksum that reduces to the same value as ours, so checksum_ok is
// true and the field map reads identically.
#include <ESP32IRPulseKit.h>

#ifndef IR_RX_GPIO
#define IR_RX_GPIO "32"
#endif

#ifndef IR_RX_INVERTED
#define IR_RX_INVERTED "1"
#endif

const int kIrRxGpio = atoi(IR_RX_GPIO);
const bool kIrRxInverted = atoi(IR_RX_INVERTED) != 0;

esp32irpk::IRReceiver<> rx(kIrRxGpio, kIrRxInverted);

namespace
{
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
  Serial.print("RX_READY impl=ESP32IRPulseKit gpio=");
  Serial.print(kIrRxGpio);
  Serial.print(" inverted=");
  Serial.println(kIrRxInverted ? 1 : 0);
}

bool readLine(String &line)
{
  if (!Serial.available())
    return false;
  line = Serial.readStringUntil('\n');
  line.trim();
  return line.length() > 0;
}

void printAcDecode(const esp32irpk::IRRawTickView &raw)
{
  esp32irpk::ac::Fujitsu::Frame f;
  if (!esp32irpk::ac::Fujitsu::Frame::fromRaw(raw, f))
  {
    Serial.print("AC_RAW vendor=NONE raw_len=");
    Serial.println(raw.len);
    return;
  }

  Serial.print("AC_DECODE vendor=FUJITSU checksum=");
  Serial.print(f.checksum_ok ? "ok" : "bad");
  Serial.print(" power=");
  Serial.print(f.power() ? 1 : 0);
  Serial.print(" mode=");
  Serial.print((unsigned)f.mode());
  Serial.print(" temp=");
  Serial.print((unsigned)f.temperatureC());
  Serial.print(" fan=");
  Serial.print((unsigned)f.fan());
  Serial.print(" swing=");
  Serial.print((unsigned)f.swing());
  Serial.print(" bytes=");
  printStateHex(f.bytes, f.byte_length);
  Serial.println();
}
} // namespace

void setup()
{
  Serial.begin(115200);
  delay(5000);

  rx.setDecodeCandidates(0);     // RAW-only: no generic decode
  rx.setMaxRxSymbols(1024);      // hold the full 16-byte frame
  rx.setIdleThresholdUs(100000); // 100ms > frame duration, < ~300ms ceiling
  if (!rx.begin())
  {
    Serial.println("RX_ERROR begin_failed");
    return;
  }
  sendReady();
}

void loop()
{
  String line;
  if (readLine(line))
  {
    if (line == "PING")
      Serial.println("PONG");
    else if (line == "READY")
      sendReady();
  }

  esp32irpk::IRReceiveResult<> r;
  if (rx.read(r))
  {
    if (r.raw.len > 0)
      printAcDecode(r.raw);
    delay(1);
  }

  delay(1);
}
