// Compat-AC encoder verification (Carrier) -- RX side (IRremoteESP8266).
//
// The peer TX (peer_tx/) is ESP32IRPulseKit: it encodes a known Carrier state
// (8-byte / 64-bit CARRIER_AC64) with esp32irpk::ac::Carrier and transmits it. This
// primary receives with IRremoteESP8266's IRrecv and decodes with IRCarrierAc64,
// printing the recovered fields and 8 bytes, so the harness can confirm an
// INDEPENDENT stack accepts our encoder's burst and recovers the same bytes with a
// valid checksum.
//
// Bytes are printed LSB-first (byte 0 = 0x84 signature ... byte 7) to match our
// wire byte order. IRCarrierAc64 field codes match ours (mode heat1/cool2/fan3;
// fan auto0..high3).
#include <IRrecv.h>
#include <IRutils.h>
#include <ir_Carrier.h>

#ifndef IR_RX_GPIO
#define IR_RX_GPIO "32"
#endif

#ifndef IR_RX_INVERTED
#define IR_RX_INVERTED "1"
#endif

const int kIrRxGpio = atoi(IR_RX_GPIO);
const bool kIrRxInverted = atoi(IR_RX_INVERTED) != 0;

const uint16_t kRxCaptureBufferSize = 512;
const uint8_t kRxTimeoutMs = 15; // single 64-bit frame; only bit spaces (<=1.7ms) inside

IRrecv irrecv(kIrRxGpio, kRxCaptureBufferSize, kRxTimeoutMs, true);
decode_results results;

namespace
{
// Print the 64-bit value LSB-first, matching esp32irpk::ac::Carrier's wire order.
void printStateHex(uint64_t state)
{
  for (int i = 0; i < 8; ++i)
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
  IRCarrierAc64 ac(0);
  ac.setRaw(r.value);
  const bool checksum_ok = IRCarrierAc64::validChecksum(r.value);

  Serial.print("AC_DECODE vendor=CARRIER checksum=");
  Serial.print(checksum_ok ? "ok" : "bad");
  Serial.print(" power=");
  Serial.print(ac.getPower() ? 1 : 0);
  Serial.print(" mode=");
  Serial.print(ac.getMode());
  Serial.print(" temp=");
  Serial.print((int)ac.getTemp());
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
    if (results.decode_type == decode_type_t::CARRIER_AC64 &&
        results.bits == kCarrierAc64Bits)
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
