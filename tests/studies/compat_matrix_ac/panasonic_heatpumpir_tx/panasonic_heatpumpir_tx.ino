// Compat-AC second reference -- RX side (ESP32IRPulseKit).
//
// The peer TX (peer_tx/) is HeatpumpIR (a codebase independent of both us and
// IRremoteESP8266), encoding a known Panasonic A/C state and transmitting it on
// an LEDC carrier. This primary captures it with our RAW path and decodes it
// with esp32irpk::ac::Panasonic. A second independent encoder agreeing with our
// decoder is stronger evidence for the field map than IRremoteESP8266 alone.
//
// Identical RX setup to irremoteesp8266_tx: RAW-only + enlarged capture + a long
// idle so the two sections land in one capture. One AC_DECODE line per burst.
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
  esp32irpk::ac::Panasonic::Frame f;
  if (!esp32irpk::ac::Panasonic::Frame::fromRaw(raw, f))
  {
    // Dump the captured ticks (1 tick = 10us) so a decode failure can be
    // diagnosed against the expected Panasonic timing.
    Serial.print("AC_RAW vendor=NONE raw_len=");
    Serial.print(raw.len);
    Serial.print(" ticks=");
    for (size_t i = 0; i < raw.len; ++i)
    {
      if (i)
        Serial.print(',');
      Serial.print((unsigned)raw.ticks[i]);
    }
    Serial.println();
    return;
  }

  Serial.print("AC_DECODE vendor=PANASONIC checksum=");
  Serial.print(f.checksum_ok ? "ok" : "bad");
  Serial.print(" power=");
  Serial.print(f.power() ? 1 : 0);
  Serial.print(" mode=");
  Serial.print((unsigned)f.mode());
  Serial.print(" temp=");
  Serial.print((unsigned)f.temperatureC());
  Serial.print(" fan=");
  Serial.print((unsigned)f.fan());
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
  rx.setMaxRxSymbols(1024);      // hold a full two-section AC burst
  rx.setIdleThresholdUs(100000); // 100ms > 10ms section gap, < ~300ms ceiling
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
