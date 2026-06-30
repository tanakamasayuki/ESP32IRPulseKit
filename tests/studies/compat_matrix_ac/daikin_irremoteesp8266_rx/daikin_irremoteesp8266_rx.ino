// Compat-AC encoder verification (Daikin) -- RX side (IRremoteESP8266).
//
// The peer TX (peer_tx/) is ESP32IRPulseKit: it encodes a known Daikin A/C state
// (classic ARC433) with esp32irpk::ac::Daikin and transmits it (5-bit preamble +
// three sections). This primary receives with IRremoteESP8266's IRrecv and decodes
// with IRDaikinESP, printing the recovered fields and 35 bytes. The harness checks
// that an INDEPENDENT stack accepts our encoder's burst and decodes it to the same
// 35 bytes with valid section checksums -- i.e. our toRaw() (preamble + sections)
// produces a well-formed Daikin burst.
//
// A 65ms end-of-message timeout exceeds the ~29ms inter-section gap so all three
// sections land in one capture.
#include <IRrecv.h>
#include <IRutils.h>
#include <ir_Daikin.h>

#ifndef IR_RX_GPIO
#define IR_RX_GPIO "32"
#endif

#ifndef IR_RX_INVERTED
#define IR_RX_INVERTED "1"
#endif

const int kIrRxGpio = atoi(IR_RX_GPIO);
const bool kIrRxInverted = atoi(IR_RX_INVERTED) != 0;

const uint16_t kRxCaptureBufferSize = 1024;
const uint8_t kRxTimeoutMs = 65;

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
  // validChecksum() takes a non-const pointer; r.state is const, so work on a copy.
  uint8_t state[kDaikinStateLength];
  for (uint16_t i = 0; i < kDaikinStateLength; ++i)
    state[i] = r.state[i];
  IRDaikinESP ac(0);
  ac.setRaw(state);
  const bool checksum_ok = IRDaikinESP::validChecksum(state);

  Serial.print("AC_DECODE vendor=DAIKIN checksum=");
  Serial.print(checksum_ok ? "ok" : "bad");
  Serial.print(" power=");
  Serial.print(ac.getPower() ? 1 : 0);
  Serial.print(" mode=");
  Serial.print(ac.getMode());
  Serial.print(" temp=");
  Serial.print((int)ac.getTemp());
  Serial.print(" fan=");
  Serial.print(ac.getFan());
  Serial.print(" swingv=");
  Serial.print(ac.getSwingVertical() ? 1 : 0);
  Serial.print(" swingh=");
  Serial.print(ac.getSwingHorizontal() ? 1 : 0);
  Serial.print(" bytes=");
  printStateHex(r.state, kDaikinStateLength);
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
    if (results.decode_type == decode_type_t::DAIKIN)
      printAcDecode(results);
    else
    {
      Serial.print("AC_RAW decode_type=");
      Serial.print((int)results.decode_type);
      Serial.print(" raw_len=");
      Serial.println(results.rawlen);
      // Dump captured durations (us) to diagnose a decode failure against the
      // expected timing (hdr 3650/1623, bit mark 428, one 1280, zero 428,
      // inter-section gap 29000).
      Serial.print("AC_TICKS_US");
      for (uint16_t i = 1; i < results.rawlen; ++i)
      {
        Serial.print(' ');
        Serial.print((uint32_t)results.rawbuf[i] * kRawTick);
      }
      Serial.println();
    }
    irrecv.resume();
  }

  delay(1);
}
