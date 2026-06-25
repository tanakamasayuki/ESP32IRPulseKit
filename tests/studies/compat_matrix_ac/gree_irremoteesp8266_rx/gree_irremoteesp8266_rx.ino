// Compat-AC encoder verification (Gree) -- RX side (IRremoteESP8266).
//
// The peer TX (peer_tx/) is ESP32IRPulseKit: it encodes a known Gree A/C state
// with esp32irpk::ac::Gree and transmits it. This primary receives with
// IRremoteESP8266's IRrecv and decodes with IRGreeAC, printing the recovered
// fields and 8 bytes in the shared compat_matrix_ac format. The harness checks
// that an INDEPENDENT stack accepts our encoder's frame and decodes it to the
// same 8 bytes with a valid checksum -- i.e. our toRaw() produces a well-formed
// Gree A/C burst.
//
// decodeGree reads the 19980us (~20ms) inter-block gap as block 2's header
// space, so the end-of-message timeout must comfortably exceed it or the second
// block gets split off and the frame is rejected (rawlen too short -> AC_RAW).
// 50ms gives ~2.5x margin and matches IRremoteESP8266's standard A/C timeout; a
// thin 30ms margin intermittently splits the capture (~half the sends drop).
#include <IRrecv.h>
#include <IRutils.h>
#include <ir_Gree.h>

#ifndef IR_RX_GPIO
#define IR_RX_GPIO "32"
#endif

#ifndef IR_RX_INVERTED
#define IR_RX_INVERTED "1"
#endif

const int kIrRxGpio = atoi(IR_RX_GPIO);
const bool kIrRxInverted = atoi(IR_RX_INVERTED) != 0;

const uint16_t kRxCaptureBufferSize = 1024;
const uint8_t kRxTimeoutMs = 50;

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
  IRGreeAC ac(0);
  ac.setRaw(r.state);
  const bool checksum_ok =
      IRGreeAC::validChecksum(r.state, kGreeStateLength);

  Serial.print("AC_DECODE vendor=GREE checksum=");
  Serial.print(checksum_ok ? "ok" : "bad");
  Serial.print(" power=");
  Serial.print(ac.getPower() ? 1 : 0);
  Serial.print(" mode=");
  Serial.print(ac.getMode());
  Serial.print(" temp=");
  Serial.print(ac.getTemp());
  Serial.print(" fan=");
  Serial.print(ac.getFan());
  Serial.print(" bytes=");
  printStateHex(r.state, kGreeStateLength);
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
    if (results.decode_type == decode_type_t::GREE)
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
