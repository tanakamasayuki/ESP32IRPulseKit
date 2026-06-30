// Compat-AC field-map calibration (Samsung) -- RX side (ESP32IRPulseKit).
//
// The peer TX (peer_tx/) is IRremoteESP8266, which encodes a KNOWN Samsung A/C
// state (standard 14-byte SAMSUNG_AC) to its canonical bytes and transmits it. This
// primary captures that burst (LSB-first, leading header + two 7-byte sections)
// with our RAW path and decodes it with esp32irpk::ac::Samsung, printing both the
// recovered 14 bytes and our interpreted fields. The harness compares our bytes vs
// the peer's canonical bytes (validates capture + section framing) and our fields
// vs the known state.
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
  esp32irpk::ac::Samsung::Frame f;
  if (!esp32irpk::ac::Samsung::Frame::fromRaw(raw, f))
  {
    Serial.print("AC_RAW vendor=NONE raw_len=");
    Serial.println(raw.len);
    return;
  }

  Serial.print("AC_DECODE vendor=SAMSUNG checksum=");
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

  rx.setDecodeCandidates(0);     // RAW-only
  rx.setMaxRxSymbols(512);       // leading header + two sections (~234 symbols)
  rx.setIdleThresholdUs(100000); // > 17.8ms leading-header space, < inter-message gap
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
