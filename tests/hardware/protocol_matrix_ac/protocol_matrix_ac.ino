// Hardware protocol matrix (A/C) -- RX side (ESP32IRPulseKit).
//
// The A/C analog of protocol_matrix: both TX and RX are ESP32IRPulseKit, so this
// is a self round-trip gate for the esp32irpk::ac layer on real hardware. The
// peer TX (peer_tx/) sends each vendor's default known-good state via ac::send;
// this RX captures it RAW, identifies the vendor with ac::decodeAny, then
// re-decodes with that vendor's Frame to echo the recovered state bytes and
// checksum. The harness asserts the recovered vendor + bytes match what the peer
// reported sending. No external library is involved -- cross-implementation
// interop is what the studies/compat_matrix_ac studies cover.
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

// Re-decode the matched vendor's frame and print a parseable line. The template
// lives in the anonymous namespace so the .ino auto-prototyper does not choke on
// a free template function.
template <class Frame>
void emit(const esp32irpk::IRRawTickView &raw, const char *vendor)
{
  Frame f{};
  if (!Frame::fromRaw(raw, f))
  {
    Serial.print("AC_RAW vendor=NONE raw_len=");
    Serial.println(raw.len);
    return;
  }
  Serial.print("AC_DECODE vendor=");
  Serial.print(vendor);
  Serial.print(" checksum=");
  Serial.print(f.checksum_ok ? "ok" : "bad");
  Serial.print(" bytes=");
  printStateHex(f.bytes, f.byte_length);
  Serial.println();
  // Drain the UART: the 35-byte Daikin line is long, and back-to-back decodes can
  // otherwise overflow the ring buffer and drop a line's tail (merging lines).
  Serial.flush();
}

void printAcDecode(const esp32irpk::IRRawTickView &raw)
{
  namespace ac = esp32irpk::ac;
  using ac::AcVendor;
  switch (ac::decodeAny(raw))
  {
  case AcVendor::PANASONIC:  emit<ac::Panasonic::Frame>(raw, "PANASONIC"); break;
  case AcVendor::GREE:       emit<ac::Gree::Frame>(raw, "GREE"); break;
  case AcVendor::MITSUBISHI: emit<ac::Mitsubishi::Frame>(raw, "MITSUBISHI"); break;
  case AcVendor::FUJITSU:    emit<ac::Fujitsu::Frame>(raw, "FUJITSU"); break;
  case AcVendor::DAIKIN:     emit<ac::Daikin::Frame>(raw, "DAIKIN"); break;
  case AcVendor::TOSHIBA:    emit<ac::Toshiba::Frame>(raw, "TOSHIBA"); break;
  case AcVendor::SAMSUNG:    emit<ac::Samsung::Frame>(raw, "SAMSUNG"); break;
  case AcVendor::SHARP:      emit<ac::Sharp::Frame>(raw, "SHARP"); break;
  case AcVendor::KELVINATOR: emit<ac::Kelvinator::Frame>(raw, "KELVINATOR"); break;
  case AcVendor::MIDEA:      emit<ac::Midea::Frame>(raw, "MIDEA"); break;
  case AcVendor::CARRIER:    emit<ac::Carrier::Frame>(raw, "CARRIER"); break;
  case AcVendor::HITACHI:    emit<ac::Hitachi::Frame>(raw, "HITACHI"); break;
  case AcVendor::HAIER:      emit<ac::Haier::Frame>(raw, "HAIER"); break;
  case AcVendor::MITSUBISHI_HEAVY: emit<ac::MitsubishiHeavy::Frame>(raw, "MITSUBISHI_HEAVY"); break;
  default:
    Serial.print("AC_RAW vendor=NONE raw_len=");
    Serial.println(raw.len);
    break;
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
} // namespace

void setup()
{
  Serial.begin(115200);
  delay(5000);

  rx.setDecodeCandidates(0);     // RAW-only: the ac:: layer works off raw ticks
  rx.setMaxRxSymbols(1024);      // hold the longest frame (35-byte Daikin, 3 sections)
  rx.setIdleThresholdUs(100000); // 100ms > Daikin's ~29ms inter-section gap
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

  esp32irpk::IRReceiveResult<> result;
  if (rx.read(result))
  {
    if (result.raw.len > 0)
      printAcDecode(result.raw);
  }

  delay(1);
}
