// Compat-AC encoder verification (Fujitsu) -- RX side (IRremoteESP8266).
//
// The peer TX (peer_tx/) is ESP32IRPulseKit: it encodes a known Fujitsu A/C state
// with esp32irpk::ac::Fujitsu and transmits it (once, as a real remote does).
// This primary receives with IRremoteESP8266's IRrecv and decodes with
// IRFujitsuAC, printing the recovered fields and bytes. The harness checks that
// an INDEPENDENT stack accepts our encoder's frame and decodes it to the same
// bytes with a valid checksum -- i.e. our toRaw() produces a well-formed Fujitsu
// A/C burst (long 16-byte and power-off short 7-byte frames).
//
// A 50ms end-of-message timeout comfortably exceeds the frame duration, so the
// whole frame lands in one capture.
#include <IRrecv.h>
#include <IRutils.h>
#include <ir_Fujitsu.h>

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
  const uint16_t nbytes = r.bits / 8;
  IRFujitsuAC ac(0);
  ac.setRaw(r.state, nbytes);
  const bool checksum_ok = IRFujitsuAC::validChecksum(
      const_cast<uint8_t *>(r.state), nbytes);

  Serial.print("AC_DECODE vendor=FUJITSU checksum=");
  Serial.print(checksum_ok ? "ok" : "bad");
  Serial.print(" power=");
  Serial.print(ac.getPower() ? 1 : 0);
  Serial.print(" mode=");
  Serial.print(ac.getMode());
  Serial.print(" temp=");
  Serial.print((int)ac.getTemp());
  Serial.print(" fan=");
  Serial.print(ac.getFanSpeed());
  Serial.print(" swing=");
  Serial.print(ac.getSwing());
  Serial.print(" bytes=");
  printStateHex(r.state, nbytes);
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
    if (results.decode_type == decode_type_t::FUJITSU_AC)
      printAcDecode(results);
    else
    {
      Serial.print("AC_RAW decode_type=");
      Serial.print((int)results.decode_type);
      Serial.print(" raw_len=");
      Serial.println(results.rawlen);
      // Dump captured durations (us) to diagnose a decode failure against the
      // expected timing (hdr 3324/1574, bit mark 448, one 1182, zero 390,
      // min gap 8100).
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
