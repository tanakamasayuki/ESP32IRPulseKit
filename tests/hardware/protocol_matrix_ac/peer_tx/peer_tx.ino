// Hardware protocol matrix (A/C) -- TX side (ESP32IRPulseKit).
//
// Sends each vendor's default known-good state (the Frame's built-in template) via
// its toRaw() and the RAW sender, then echoes the exact bytes on the wire
// (recovered with our own fromRaw) so the RX primary can compare its recovered
// state against them. One representative state per vendor is enough for a self
// round-trip gate; per-field coverage is exercised on the host by codec_smoke.
//
// Carrier defaults to phase-aligned (the library default). Some A/C vendors
// (Gree, Daikin) require it, so this gate does not offer a hardware-carrier mode.
// Peer name "tx" reuses TEST_SERIAL_PORT_PEER_TX_TX_ESP32S3.
//
// Command: SEND_AC vendor=<PANASONIC|GREE|MITSUBISHI|FUJITSU|DAIKIN|TOSHIBA|SAMSUNG|SHARP|KELVINATOR|MIDEA|CARRIER|HITACHI|HAIER>
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
  Serial.println(" carrier=pa");
}

// Encode a vendor's default Frame, transmit it, and echo the on-wire bytes. The
// template lives in the anonymous namespace so the .ino auto-prototyper does not
// choke on a free template function.
template <class Frame>
void sendVendor(const char *vendor)
{
  Frame f{}; // default known-good state
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
  Serial.print("TX_OK_AC vendor=");
  Serial.print(vendor);
  Serial.print(" bytes=");
  printStateHex(canonical.bytes, canonical.byte_length);
  Serial.println();
}

void handleSendAc(const String &line)
{
  namespace ac = esp32irpk::ac;
  const String v = fieldValue(line, "vendor");
  if (v == "PANASONIC")       sendVendor<ac::Panasonic::Frame>("PANASONIC");
  else if (v == "GREE")       sendVendor<ac::Gree::Frame>("GREE");
  else if (v == "MITSUBISHI") sendVendor<ac::Mitsubishi::Frame>("MITSUBISHI");
  else if (v == "FUJITSU")    sendVendor<ac::Fujitsu::Frame>("FUJITSU");
  else if (v == "DAIKIN")     sendVendor<ac::Daikin::Frame>("DAIKIN");
  else if (v == "TOSHIBA")    sendVendor<ac::Toshiba::Frame>("TOSHIBA");
  else if (v == "SAMSUNG")    sendVendor<ac::Samsung::Frame>("SAMSUNG");
  else if (v == "SHARP")      sendVendor<ac::Sharp::Frame>("SHARP");
  else if (v == "KELVINATOR") sendVendor<ac::Kelvinator::Frame>("KELVINATOR");
  else if (v == "MIDEA")      sendVendor<ac::Midea::Frame>("MIDEA");
  else if (v == "CARRIER")    sendVendor<ac::Carrier::Frame>("CARRIER");
  else if (v == "HITACHI")    sendVendor<ac::Hitachi::Frame>("HITACHI");
  else if (v == "HAIER")      sendVendor<ac::Haier::Frame>("HAIER");
  else
    Serial.println("TX_ERROR unsupported_vendor");
}

void handleCommand(const String &line)
{
  if (line == "PING")
    Serial.println("PONG");
  else if (line == "READY")
    sendReady();
  else if (line.startsWith("SEND_AC"))
    handleSendAc(line);
  else
  {
    Serial.print("TX_ERROR unknown_command ");
    Serial.println(line);
  }
}
} // namespace

void setup()
{
  Serial.begin(115200);
  delay(5000);
  tx.setPhaseAlignedCarrier(true); // library default; required by some A/C vendors
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
