// JVC timing-sweep RX: receives with IRremoteESP8266 and dumps the raw capture
// on EVERY frame (decoded or not), so the host can read the received zero-space
// even when the frame decodes. Pairs with a PulseKit peer TX that emits JVC with
// tunable mark / zero-space / one-space (peer_tx/), to map which emitted timing
// keeps the received zero-space under IRremoteESP8266's fixed ~595 us JVC window.
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
String protocolName(const decode_results &r)
{
  switch (r.decode_type)
  {
  case NEC: return "NEC";
  case SONY: return "SONY" + String(r.bits);
  case SAMSUNG: return "SAMSUNG" + String(r.bits);
  case JVC: return "JVC";
  case UNKNOWN: return "UNKNOWN";
  default: return "OTHER_" + String((int)r.decode_type);
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
      Serial.println("PONG");
    else if (line == "READY")
      sendReady();
  }

  if (irrecv.decode(&results))
  {
    // One unified line per frame: protocol (or UNKNOWN), value, and the full raw
    // capture in us. Paced/chunked flush so the long line is not truncated.
    Serial.print("RX proto=");
    Serial.print(protocolName(results));
    Serial.print(" bits=0x");
    printBits64(results.value);
    Serial.print(" len=");
    Serial.print(results.bits);
    Serial.print(" raw_len=");
    Serial.print(results.rawlen);
    Serial.print(" us=");
    Serial.flush();
    delay(8);
    for (uint16_t i = 0; i < results.rawlen; ++i)
    {
      if (i > 0)
        Serial.print(',');
      Serial.print((uint32_t)results.rawbuf[i] * kRawTick);
      if ((i & 0x0F) == 0x0F)
      {
        Serial.flush();
        delay(8);
      }
    }
    Serial.println();
    Serial.flush();
    irrecv.resume();
  }

  delay(1);
}
