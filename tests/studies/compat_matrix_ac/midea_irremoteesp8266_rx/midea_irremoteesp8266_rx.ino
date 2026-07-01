// Compat-AC encoder verification (Midea) -- RX side (IRremoteESP8266).
//
// The peer TX (peer_tx/) is ESP32IRPulseKit: it encodes a known Midea state
// (48-bit / 6-byte) with esp32irpk::ac::Midea and transmits it (data + inverted
// copy). This primary receives with IRremoteESP8266's IRrecv and decodes with
// IRMideaAC, printing the recovered fields and 6 bytes, so the harness can confirm
// an INDEPENDENT stack accepts our encoder's double burst and recovers the same
// bytes with a valid checksum.
//
// Bytes are printed MSB-first (byte 0 = 0xA1 header ... byte 5 = checksum) to match
// our wire byte order. IRMideaAC field codes match ours (mode cool0..fan4; fan
// auto0..high3). The peer sends Celsius states, so getTemp(true) is the C value.
#include <IRrecv.h>
#include <IRutils.h>
#include <ir_Midea.h>

#ifndef IR_RX_GPIO
#define IR_RX_GPIO "32"
#endif

#ifndef IR_RX_INVERTED
#define IR_RX_INVERTED "1"
#endif

const int kIrRxGpio = atoi(IR_RX_GPIO);
const bool kIrRxInverted = atoi(IR_RX_INVERTED) != 0;

const uint16_t kRxCaptureBufferSize = 512;
const uint8_t kRxTimeoutMs = 15; // > the 5.6ms inter-copy gap so both copies are one capture

IRrecv irrecv(kIrRxGpio, kRxCaptureBufferSize, kRxTimeoutMs, true);
decode_results results;

namespace
{
// Print the 48-bit value MSB-first, matching esp32irpk::ac::Midea's wire order.
void printStateHex(uint64_t state)
{
  for (int i = 5; i >= 0; --i)
  {
    uint8_t b = (uint8_t)((state >> (i * 8)) & 0xFF);
    if (b < 0x10)
      Serial.print('0');
    Serial.print(b, HEX);
  }
}

void sendReady()
{
  Serial.print("RX_READY impl=IRremoteESP8266 gpio=");
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

void printAcDecode(const decode_results &r)
{
  IRMideaAC ac(0);
  ac.setRaw(r.value);
  const bool checksum_ok = IRMideaAC::validChecksum(r.value);

  Serial.print("AC_DECODE vendor=MIDEA checksum=");
  Serial.print(checksum_ok ? "ok" : "bad");
  Serial.print(" power=");
  Serial.print(ac.getPower() ? 1 : 0);
  Serial.print(" mode=");
  Serial.print(ac.getMode());
  Serial.print(" temp=");
  Serial.print((int)ac.getTemp(true)); // Celsius
  Serial.print(" fan=");
  Serial.print(ac.getFan()); // wire code: auto0..high3
  Serial.print(" bytes=");
  printStateHex(r.value);
  Serial.println();
}
} // namespace

void setup()
{
  Serial.begin(115200);
  delay(5000);
  irrecv.enableIRIn();
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

  if (irrecv.decode(&results))
  {
    if (results.decode_type == decode_type_t::MIDEA &&
        results.bits == kMideaBits)
      printAcDecode(results);
    else
    {
      Serial.print("AC_RAW decode_type=");
      Serial.print((int)results.decode_type);
      Serial.print(" raw_len=");
      Serial.println(results.rawlen);
    }
    irrecv.resume();
  }

  delay(1);
}
