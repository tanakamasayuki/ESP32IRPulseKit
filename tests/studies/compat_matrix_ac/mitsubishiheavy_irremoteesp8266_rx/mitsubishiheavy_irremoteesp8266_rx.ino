// Compat-AC encoder verification (Mitsubishi Heavy) -- RX side (IRremoteESP8266).
//
// The peer TX (peer_tx/) is ESP32IRPulseKit: it encodes a known Mitsubishi Heavy
// state (19-byte MITSUBISHI_HEAVY_152) with esp32irpk::ac::MitsubishiHeavy and
// transmits it. This primary receives with IRremoteESP8266's IRrecv and decodes
// with IRMitsubishiHeavy152Ac, printing the recovered fields and 19 bytes, so the
// harness can confirm an INDEPENDENT stack accepts our encoder's burst and recovers
// the same bytes with valid inverted byte pairs.
//
// Bytes are printed in transmission order (byte 0 = 0xAD signature first) to match
// our byte order. IRMitsubishiHeavy152Ac field codes match ours (mode auto0/cool1/
// dry2/fan3/heat4; fan auto0/low1/med2/high3/max4).
#include <IRrecv.h>
#include <IRutils.h>
#include <ir_MitsubishiHeavy.h>

#ifndef IR_RX_GPIO
#define IR_RX_GPIO "32"
#endif

#ifndef IR_RX_INVERTED
#define IR_RX_INVERTED "1"
#endif

const int kIrRxGpio = atoi(IR_RX_GPIO);
const bool kIrRxInverted = atoi(IR_RX_INVERTED) != 0;

const uint16_t kRxCaptureBufferSize = 1024;
const uint8_t kRxTimeoutMs = 15; // > the 1.63ms header space inside the frame

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
  uint8_t state[kMitsubishiHeavy152StateLength];
  for (uint16_t i = 0; i < kMitsubishiHeavy152StateLength; ++i)
    state[i] = r.state[i];
  IRMitsubishiHeavy152Ac ac(0);
  ac.setRaw(state);
  const bool checksum_ok =
      IRMitsubishiHeavy152Ac::validChecksum(state, kMitsubishiHeavy152StateLength);

  Serial.print("AC_DECODE vendor=MITSUBISHI_HEAVY checksum=");
  Serial.print(checksum_ok ? "ok" : "bad");
  Serial.print(" power=");
  Serial.print(ac.getPower() ? 1 : 0);
  Serial.print(" mode=");
  Serial.print(ac.getMode());
  Serial.print(" temp=");
  Serial.print((int)ac.getTemp());
  Serial.print(" fan=");
  Serial.print(ac.getFan());
  Serial.print(" bytes=");
  printStateHex(state, kMitsubishiHeavy152StateLength);
  Serial.println();
  Serial.flush();
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
    if (results.decode_type == decode_type_t::MITSUBISHI_HEAVY_152 &&
        results.bits == kMitsubishiHeavy152Bits)
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
