// Compat-AC encoder verification (TCL) -- RX side (IRremoteESP8266).
//
// The peer TX (peer_tx/) is ESP32IRPulseKit: it encodes a known TCL state (14-byte
// TCL112AC) with esp32irpk::ac::Tcl and transmits it. This primary receives with
// IRremoteESP8266's IRrecv and decodes with IRTcl112Ac, printing the recovered
// fields and 14 bytes, so the harness can confirm an INDEPENDENT stack accepts our
// encoder's burst and recovers the same bytes with a valid checksum.
//
// TCL112AC and Mitsubishi112 share a decoder; the reference tells them apart by the
// header + byte 2 == 0x26, so our frame classifies as TCL112AC. Bytes are printed in
// transmission order (byte 0 = 0x23 signature first) to match our byte order.
#include <IRrecv.h>
#include <IRutils.h>
#include <ir_Tcl.h>

#ifndef IR_RX_GPIO
#define IR_RX_GPIO "32"
#endif

#ifndef IR_RX_INVERTED
#define IR_RX_INVERTED "1"
#endif

const int kIrRxGpio = atoi(IR_RX_GPIO);
const bool kIrRxInverted = atoi(IR_RX_INVERTED) != 0;

const uint16_t kRxCaptureBufferSize = 1024;
const uint8_t kRxTimeoutMs = 15; // > the 1.65ms header space inside the frame

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
  uint8_t state[kTcl112AcStateLength];
  for (uint16_t i = 0; i < kTcl112AcStateLength; ++i)
    state[i] = r.state[i];
  IRTcl112Ac ac(0);
  ac.setRaw(state);
  const bool checksum_ok = IRTcl112Ac::validChecksum(state, kTcl112AcStateLength);

  Serial.print("AC_DECODE vendor=TCL checksum=");
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
  printStateHex(state, kTcl112AcStateLength);
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
    if (results.decode_type == decode_type_t::TCL112AC &&
        results.bits == kTcl112AcBits)
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
