// Compat-AC encoder verification (Hitachi) -- RX side (IRremoteESP8266).
//
// The peer TX (peer_tx/) is ESP32IRPulseKit: it encodes a known Hitachi state
// (28-byte HITACHI_AC) with esp32irpk::ac::Hitachi and transmits it. This primary
// receives with IRremoteESP8266's IRrecv and decodes with IRHitachiAc, printing the
// recovered fields and 28 bytes, so the harness can confirm an INDEPENDENT stack
// accepts our encoder's burst and recovers the same bytes with a valid checksum.
//
// Bytes are printed in transmission order (byte 0 first) to match our byte order.
// IRHitachiAc field codes match ours (mode auto2..fan12; fan auto1..high5).
#include <IRrecv.h>
#include <IRutils.h>
#include <ir_Hitachi.h>

#ifndef IR_RX_GPIO
#define IR_RX_GPIO "32"
#endif

#ifndef IR_RX_INVERTED
#define IR_RX_INVERTED "1"
#endif

const int kIrRxGpio = atoi(IR_RX_GPIO);
const bool kIrRxInverted = atoi(IR_RX_INVERTED) != 0;

const uint16_t kRxCaptureBufferSize = 600; // 28 bytes = 224 bits -> ~452 rawbuf entries
const uint8_t kRxTimeoutMs = 15;           // single frame; only bit spaces (<=1.25ms) inside

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
  uint8_t state[kHitachiAcStateLength];
  for (uint16_t i = 0; i < kHitachiAcStateLength; ++i)
    state[i] = r.state[i];
  IRHitachiAc ac(0);
  ac.setRaw(state, kHitachiAcStateLength);
  const bool checksum_ok = IRHitachiAc::validChecksum(state, kHitachiAcStateLength);

  Serial.print("AC_DECODE vendor=HITACHI checksum=");
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
  printStateHex(state, kHitachiAcStateLength);
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
    if (results.decode_type == decode_type_t::HITACHI_AC &&
        results.bits == kHitachiAcBits)
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
