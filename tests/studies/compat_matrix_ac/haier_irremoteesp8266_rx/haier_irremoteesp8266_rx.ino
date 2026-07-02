// Compat-AC encoder verification (Haier) -- RX side (IRremoteESP8266).
//
// The peer TX (peer_tx/) is ESP32IRPulseKit: it encodes a known Haier state (9-byte
// HAIER_AC) with esp32irpk::ac::Haier and transmits it. This primary receives with
// IRremoteESP8266's IRrecv and decodes with IRHaierAC, printing the recovered fields
// and 9 bytes, so the harness can confirm an INDEPENDENT stack accepts our encoder's
// double-header burst and recovers the same bytes with a valid checksum.
//
// Bytes are printed in transmission order (byte 0 = 0xA5 prefix first) to match our
// byte order. IRHaierAC field codes match ours (mode auto0..fan4; fan auto0..high3).
// Power is the On/Off command (getCommand()).
#include <IRrecv.h>
#include <IRutils.h>
#include <ir_Haier.h>

#ifndef IR_RX_GPIO
#define IR_RX_GPIO "32"
#endif

#ifndef IR_RX_INVERTED
#define IR_RX_INVERTED "1"
#endif

const int kIrRxGpio = atoi(IR_RX_GPIO);
const bool kIrRxInverted = atoi(IR_RX_INVERTED) != 0;

const uint16_t kRxCaptureBufferSize = 512;
const uint8_t kRxTimeoutMs = 15; // > the 4.3ms header space inside the frame

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
  uint8_t state[kHaierACStateLength];
  for (uint16_t i = 0; i < kHaierACStateLength; ++i)
    state[i] = r.state[i];
  IRHaierAC ac(0);
  ac.setRaw(state);
  const bool checksum_ok = IRHaierAC::validChecksum(state, kHaierACStateLength);

  Serial.print("AC_DECODE vendor=HAIER checksum=");
  Serial.print(checksum_ok ? "ok" : "bad");
  Serial.print(" power=");
  Serial.print(ac.getCommand() != kHaierAcCmdOff ? 1 : 0);
  Serial.print(" mode=");
  Serial.print(ac.getMode());
  Serial.print(" temp=");
  Serial.print((int)ac.getTemp());
  Serial.print(" fan=");
  Serial.print(ac.getFan());
  Serial.print(" bytes=");
  printStateHex(state, kHaierACStateLength);
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
    if (results.decode_type == decode_type_t::HAIER_AC &&
        results.bits == kHaierACBits)
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
