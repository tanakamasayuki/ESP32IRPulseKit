// Compat-AC encoder verification (Kelvinator) -- RX side (IRremoteESP8266).
//
// The peer TX (peer_tx/) is ESP32IRPulseKit: it encodes a known Kelvinator state
// (16-byte, two blocks) with esp32irpk::ac::Kelvinator and transmits it. This
// primary receives with IRremoteESP8266's IRrecv and decodes with IRKelvinatorAC,
// printing the recovered fields and 16 bytes, so the harness can confirm an
// INDEPENDENT stack accepts our encoder's two-block burst and recovers the same
// bytes with a valid checksum.
//
// IRKelvinatorAC::getFan() returns the wire code (auto=0..max=5), matching our enum
// values. Temp is a don't-care in Auto/Dry (those modes force 25C).
#include <IRrecv.h>
#include <IRutils.h>
#include <ir_Kelvinator.h>

#ifndef IR_RX_GPIO
#define IR_RX_GPIO "32"
#endif

#ifndef IR_RX_INVERTED
#define IR_RX_INVERTED "1"
#endif

const int kIrRxGpio = atoi(IR_RX_GPIO);
const bool kIrRxInverted = atoi(IR_RX_INVERTED) != 0;

const uint16_t kRxCaptureBufferSize = 512;
const uint8_t kRxTimeoutMs = 55; // > the ~40ms inter-block gap so both blocks are one capture

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
  uint8_t state[kKelvinatorStateLength];
  for (uint16_t i = 0; i < kKelvinatorStateLength; ++i)
    state[i] = r.state[i];
  IRKelvinatorAC ac(0);
  ac.setRaw(state);
  const bool checksum_ok = IRKelvinatorAC::validChecksum(state, kKelvinatorStateLength);

  Serial.print("AC_DECODE vendor=KELVINATOR checksum=");
  Serial.print(checksum_ok ? "ok" : "bad");
  Serial.print(" power=");
  Serial.print(ac.getPower() ? 1 : 0);
  Serial.print(" mode=");
  Serial.print(ac.getMode());
  Serial.print(" temp=");
  Serial.print((int)ac.getTemp());
  Serial.print(" fan=");
  Serial.print(ac.getFan()); // wire code: auto=0..max=5
  Serial.print(" bytes=");
  printStateHex(state, kKelvinatorStateLength);
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
    if (results.decode_type == decode_type_t::KELVINATOR &&
        results.bits == kKelvinatorBits)
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
