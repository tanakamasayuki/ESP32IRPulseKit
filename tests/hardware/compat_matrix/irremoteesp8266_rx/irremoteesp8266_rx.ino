// Compat primary RX: receives with IRremoteESP8266 (IRrecv) instead of
// ESP32IRPulseKit, and prints decodes in the same RX_DECODE format the pytest
// harness expects. The peer TX (peer_tx/) stays ESP32IRPulseKit, so this
// observes how an external receiver interprets our transmitter's frames.
//
// IRremoteESP8266 has no "score" metric, so score is reported as 0. Per-tick
// dumps are omitted (the harness only parses up to raw_len).
#include <IRrecv.h>
#include <IRutils.h>

#ifndef IR_RX_GPIO
#define IR_RX_GPIO "32"
#endif

#ifndef IR_RX_INVERTED
#define IR_RX_INVERTED "1"
#endif

const int kIrRxGpio = atoi(IR_RX_GPIO);
const bool kIrRxInverted = atoi(IR_RX_INVERTED) != 0;

const uint16_t kRxCaptureBufferSize = 1024;
const uint8_t kRxTimeoutMs = 15;

IRrecv irrecv(kIrRxGpio, kRxCaptureBufferSize, kRxTimeoutMs, true);
decode_results results;

namespace
{
// Map IRremoteESP8266's protocol to the uppercase names the harness regex
// expects ([A-Z0-9_]+). Width is appended to match the in-house naming
// (SONY12 / SAMSUNG32 / JVC24); NEC stays NEC.
String protocolName(const decode_results &r)
{
  switch (r.decode_type)
  {
  case NEC:
    return "NEC";
  case SONY:
    return "SONY" + String(r.bits);
  case SAMSUNG:
    return "SAMSUNG" + String(r.bits);
  case JVC:
    return "JVC" + String(r.bits);
  default:
    return "OTHER_" + String((int)r.decode_type);
  }
}

void printBits64(uint64_t bits)
{
  Serial.print((uint32_t)(bits >> 32), HEX);
  Serial.print((uint32_t)(bits & 0xffffffffu), HEX);
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
    {
      Serial.println("PONG");
    }
    else if (line == "READY")
    {
      sendReady();
    }
  }

  if (irrecv.decode(&results))
  {
    if (results.decode_type != UNKNOWN)
    {
      Serial.print("RX_DECODE protocol=");
      Serial.print(protocolName(results));
      Serial.print(" score=0 len=");
      Serial.print(results.bits);
      Serial.print(" bits=0x");
      printBits64(results.value);
      Serial.print(" type=");
      Serial.print(results.repeat ? "REPEAT" : "NORMAL");
      Serial.print(" raw_len=");
      Serial.println(results.rawlen);
    }
    irrecv.resume();
  }

  delay(1);
}
