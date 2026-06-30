// Compat-AC field-map calibration (Daikin) -- RX side (ESP32IRPulseKit).
//
// The peer TX (peer_tx/) is IRremoteESP8266, which encodes a KNOWN Daikin A/C
// state (classic ARC433) to its canonical 35 bytes and transmits it. This primary
// captures that burst (a 5-bit preamble + three sections) with our RAW path and
// decodes it with esp32irpk::ac::Daikin, printing both the recovered 35 bytes and
// our interpreted fields. The harness compares:
//   1. our bytes vs the peer's canonical bytes -> validates our RAW capture, the
//      preamble skip, and the three-section framing, independent of interpretation;
//   2. our decoded fields vs the known sent state -> calibrates the field map.
//
// The inter-section gap is ~29ms, so the idle threshold (100ms) keeps all three
// sections in one capture.
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

uint32_t g_frames = 0; // total frames returned by read() (diagnostic)

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
  esp32irpk::ac::Daikin::Frame f;
  if (!esp32irpk::ac::Daikin::Frame::fromRaw(raw, f))
  {
    Serial.print("AC_RAW vendor=NONE raw_len=");
    Serial.print(raw.len);
    // Dump the capture (us) so a decode failure can be diagnosed against the
    // expected timing (hdr 3650/1623, bit 428, one 1280, zero 428, ~29ms gap) and
    // to tell a full ~584-symbol burst from a fragment.
    Serial.print(" ticks_us=");
    for (uint16_t i = 0; i < raw.len; ++i)
    {
      if (i)
        Serial.print(',');
      Serial.print((uint32_t)raw.ticks[i] * 10);
    }
    Serial.println();
    return;
  }

  Serial.print("AC_DECODE vendor=DAIKIN checksum=");
  Serial.print(f.checksum_ok ? "ok" : "bad");
  Serial.print(" power=");
  Serial.print(f.power() ? 1 : 0);
  Serial.print(" mode=");
  Serial.print((unsigned)f.mode());
  Serial.print(" temp=");
  Serial.print((unsigned)f.temperatureC());
  Serial.print(" fan=");
  Serial.print((unsigned)f.fan());
  Serial.print(" swingv=");
  Serial.print(f.swingVertical() ? 1 : 0);
  Serial.print(" swingh=");
  Serial.print(f.swingHorizontal() ? 1 : 0);
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
  rx.setMaxRxSymbols(1024);      // hold the preamble + three sections
  rx.setIdleThresholdUs(100000); // 100ms > 29ms inter-section gap, < ~300ms ceiling
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
    {
      // Dump receiver stats so a dropped/overflowed capture is visible even when
      // read() never yields a frame for this long four-gap burst.
      esp32irpk::IRRxStats s = rx.stats();
      Serial.print("PONG frames=");
      Serial.print(g_frames);
      Serial.print(" qovf=");
      Serial.print(s.queue_overflow_count);
      Serial.print(" rmtovf=");
      Serial.print(s.rmt_overflow_count);
      Serial.print(" trunc=");
      Serial.println(s.raw_truncated_count);
    }
    else if (line == "READY")
      sendReady();
  }

  esp32irpk::IRReceiveResult<> r;
  if (rx.read(r))
  {
    ++g_frames;
    // Log every frame (len + flags) so truncation/overflow is visible regardless
    // of whether fromRaw succeeds.
    Serial.print("RX_FRAME len=");
    Serial.print(r.raw.len);
    Serial.print(" flags=0x");
    Serial.println((unsigned)r.flags, HEX);
    if (r.raw.len > 0)
      printAcDecode(r.raw);
    delay(1);
  }

  delay(1);
}
