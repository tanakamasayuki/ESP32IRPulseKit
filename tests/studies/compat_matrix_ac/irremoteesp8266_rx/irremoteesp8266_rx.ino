// Compat-AC encoder verification -- RX side (IRremoteESP8266).
//
// The peer TX (peer_tx/) is ESP32IRPulseKit: it encodes a known Panasonic A/C
// state with esp32irpk::ac::Panasonic and transmits it. This primary receives
// with IRremoteESP8266's IRrecv and decodes with IRPanasonicAc, printing the
// recovered fields and 27 bytes in the shared compat_matrix_ac format. The
// harness checks that an INDEPENDENT stack accepts our encoder's frame and
// decodes it to the same 27 bytes with a valid checksum -- i.e. our toRaw()
// produces a well-formed Panasonic A/C burst.
//
// The 10ms inter-section gap must stay below the end-of-message timeout so both
// sections land in one capture, hence 20ms.
#include <IRrecv.h>
#include <IRutils.h>
#include <ir_Panasonic.h>

#ifndef IR_RX_GPIO
#define IR_RX_GPIO "32"
#endif

#ifndef IR_RX_INVERTED
#define IR_RX_INVERTED "1"
#endif

const int kIrRxGpio = atoi(IR_RX_GPIO);
const bool kIrRxInverted = atoi(IR_RX_INVERTED) != 0;

const uint16_t kRxCaptureBufferSize = 1024;
const uint8_t kRxTimeoutMs = 20;

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
  IRPanasonicAc ac(0);
  ac.setRaw(r.state);
  const bool checksum_ok =
      IRPanasonicAc::validChecksum(r.state, kPanasonicAcStateLength);

  Serial.print("AC_DECODE vendor=PANASONIC checksum=");
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
  printStateHex(r.state, kPanasonicAcStateLength);
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
    if (results.decode_type == decode_type_t::PANASONIC_AC)
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
