// Compat-AC field-map calibration (Gree) -- RX side (ESP32IRPulseKit).
//
// The peer TX (peer_tx/) is IRremoteESP8266, which encodes a KNOWN Gree A/C
// state to its canonical 8 bytes and transmits it. This primary captures that
// burst with our RAW path and decodes it with esp32irpk::ac::Gree, printing both
// the recovered 8 bytes and our interpreted fields. The harness compares:
//   1. our bytes vs the peer's canonical bytes -> validates our RAW capture and
//      the two-block (no-header second block) framing, independent of how we
//      interpret the bytes;
//   2. our decoded fields vs the known sent state -> calibrates the field map
//      (byte/bit positions and mode/fan codes) in src/ac/Gree.h.
//
// AC receive mode = RAW-only (setDecodeCandidates(0)) + enlarged capture
// (setMaxRxSymbols) + a long idle so both blocks land in one capture.
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

// Decode the captured RAW with our Gree backend and emit one AC_DECODE line.
// mode/fan are our esp32irpk::ac::Gree::Mode / Fan underlying values (the codes
// under calibration); bytes is the full recovered state so the harness can diff
// it against the peer's canonical bytes.
void printAcDecode(const esp32irpk::IRRawTickView &raw)
{
  esp32irpk::ac::Gree::Frame f;
  if (!esp32irpk::ac::Gree::Frame::fromRaw(raw, f))
  {
    Serial.print("AC_RAW vendor=NONE raw_len=");
    Serial.println(raw.len);
    return;
  }

  Serial.print("AC_DECODE vendor=GREE checksum=");
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
  rx.setMaxRxSymbols(1024);      // hold a full two-block AC burst
  rx.setIdleThresholdUs(100000); // 100ms > 20ms block gap, < ~300ms ceiling
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
