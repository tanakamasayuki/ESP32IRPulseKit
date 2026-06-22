// Compat primary RX: receives with Arduino-IRremote (IrReceiver) instead of
// ESP32IRPulseKit, and prints decodes in the same RX_DECODE format the pytest
// harness expects. The peer TX (peer_tx/) stays ESP32IRPulseKit, so this
// observes how an external receiver interprets our transmitter's frames.
//
// Arduino-IRremote has no "score" metric, so score is reported as 0. When a
// frame is received but not recognized (DECODE_HASH fallback yields UNKNOWN), it
// is dumped as RX_RAW with per-entry durations in microseconds, so the harness
// can tell "received but undecodable" (a timing/tolerance difference) apart from
// "no signal at all".
#define DECODE_NEC
#define DECODE_SONY
#define DECODE_SAMSUNG
#define DECODE_JVC
#define DECODE_HASH  // report unrecognized frames as UNKNOWN (for RX_RAW)
#include <Arduino.h>
#include <IRremote.hpp>

#ifndef IR_RX_GPIO
#define IR_RX_GPIO "32"
#endif

#ifndef IR_RX_INVERTED
#define IR_RX_INVERTED "1"
#endif

const int kIrRxGpio = atoi(IR_RX_GPIO);
const bool kIrRxInverted = atoi(IR_RX_INVERTED) != 0;

namespace
{
// Map Arduino-IRremote's protocol to the uppercase names the harness regex
// expects ([A-Z0-9_]+). Width is appended to match the in-house naming
// (SONY12 / SAMSUNG32 / JVC); NEC stays NEC.
String protocolName(const IRData &d)
{
  switch (d.protocol)
  {
  case NEC:
    return "NEC";
  case SONY:
    return "SONY" + String(d.numberOfBits);
  case SAMSUNG:
    return "SAMSUNG" + String(d.numberOfBits);
  case JVC:
    return "JVC" + String(d.numberOfBits);
  default:
    return "OTHER_" + String((int)d.protocol);
  }
}

void printBits64(uint64_t bits)
{
  Serial.print((uint32_t)(bits >> 32), HEX);
  Serial.print((uint32_t)(bits & 0xffffffffu), HEX);
}

void sendReady()
{
  Serial.print("RX_READY impl=Arduino-IRremote gpio=");
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
  IrReceiver.begin(kIrRxGpio, DISABLE_LED_FEEDBACK);
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

  if (IrReceiver.decode())
  {
    const IRData &d = IrReceiver.decodedIRData;
    if (d.protocol != UNKNOWN)
    {
      Serial.print("RX_DECODE protocol=");
      Serial.print(protocolName(d));
      Serial.print(" score=0 len=");
      Serial.print(d.numberOfBits);
      Serial.print(" bits=0x");
      printBits64((uint64_t)d.decodedRawData);
      Serial.print(" type=");
      Serial.print((d.flags & IRDATA_FLAGS_IS_REPEAT) ? "REPEAT" : "NORMAL");
      Serial.print(" raw_len=");
      Serial.println((uint32_t)d.rawlen);
    }
    else
    {
      Serial.print("RX_RAW len=");
      Serial.print((uint32_t)IrReceiver.irparams.rawlen);
      Serial.print(" us=");
      for (uint16_t i = 0; i < IrReceiver.irparams.rawlen; ++i)
      {
        if (i > 0)
          Serial.print(",");
        Serial.print((uint32_t)IrReceiver.irparams.rawbuf[i] * MICROS_PER_TICK);
      }
      Serial.println();
    }
    IrReceiver.resume();
  }

  delay(1);
}
