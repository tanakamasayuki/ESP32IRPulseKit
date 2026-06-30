// Compat-AC encoder verification (Samsung) -- RX side (IRremoteESP8266).
//
// The peer TX (peer_tx/) is ESP32IRPulseKit: it encodes a known Samsung A/C state
// (standard 14-byte SAMSUNG_AC, LSB-first) with esp32irpk::ac::Samsung and transmits
// it. This primary receives with IRremoteESP8266's IRrecv and decodes with
// IRSamsungAc, printing the recovered fields and 14 bytes, so the harness can
// confirm an INDEPENDENT stack accepts our encoder's burst and recovers the same
// bytes with valid checksums.
//
// The RX timeout must exceed Samsung's 17.8ms leading-header space, or the receiver
// ends the capture before the data arrives. 30ms gives comfortable margin.
#include <IRrecv.h>
#include <IRutils.h>
#include <ir_Samsung.h>

#ifndef IR_RX_GPIO
#define IR_RX_GPIO "32"
#endif

#ifndef IR_RX_INVERTED
#define IR_RX_INVERTED "1"
#endif

const int kIrRxGpio = atoi(IR_RX_GPIO);
const bool kIrRxInverted = atoi(IR_RX_INVERTED) != 0;

const uint16_t kRxCaptureBufferSize = 512;
const uint8_t kRxTimeoutMs = 30; // > 17.8ms Samsung leading-header space

IRrecv irrecv(kIrRxGpio, kRxCaptureBufferSize, kRxTimeoutMs, true);
decode_results results;

namespace
{
void printStateHex(const uint8_t *state, uint16_t nbytes)
{
  for (uint16_t i = 0; i < nbytes; ++i)
  {
    if (state[i] < 0x10)
      Serial.print('0');
    Serial.print(state[i], HEX);
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
  // validChecksum() takes a non-const pointer; copy the const state out.
  uint8_t state[kSamsungAcStateLength];
  for (uint16_t i = 0; i < kSamsungAcStateLength; ++i)
    state[i] = r.state[i];
  IRSamsungAc ac(0);
  ac.setRaw(state, kSamsungAcStateLength);
  const bool checksum_ok = IRSamsungAc::validChecksum(state, kSamsungAcStateLength);

  Serial.print("AC_DECODE vendor=SAMSUNG checksum=");
  Serial.print(checksum_ok ? "ok" : "bad");
  Serial.print(" power=");
  Serial.print(ac.getPower() ? 1 : 0);
  Serial.print(" mode=");
  Serial.print(ac.getMode());
  Serial.print(" temp=");
  Serial.print((int)ac.getTemp());
  Serial.print(" fan=");
  Serial.print(ac.getFan()); // wire code: auto=0, low=2, med=4, high=5, turbo=7
  Serial.print(" bytes=");
  printStateHex(state, kSamsungAcStateLength);
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
    if (results.decode_type == decode_type_t::SAMSUNG_AC &&
        results.bits == kSamsungAcBits)
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
