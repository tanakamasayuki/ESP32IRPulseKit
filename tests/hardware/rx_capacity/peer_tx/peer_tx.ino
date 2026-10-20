// Hardware RX capacity test -- TX side.
//
// Sends a synthetic frame of an exact pulse count so the RX side can check how
// many symbols a capture holds. Timing is A/C-like (short marks, alternating
// short/long spaces), split into sections by a gap every kSectionPulses marks
// like a multi-section A/C frame: IR receiver modules (AGC) and the TX LED
// supply do not hold up a continuous burst of several hundred ms.
#include <ESP32IRPulseKit.h>

#include <vector>

#ifndef IR_TX_GPIO
#define IR_TX_GPIO "4"
#endif

#ifndef IR_TX_INVERTED
#define IR_TX_INVERTED "0"
#endif

const int kIrTxGpio = atoi(IR_TX_GPIO);
const bool kIrTxInverted = atoi(IR_TX_INVERTED) != 0;

esp32irpk::IRSender tx(kIrTxGpio, kIrTxInverted);

namespace
{
constexpr uint16_t kMarkTicks = 45;       // 450us
constexpr uint16_t kShortSpaceTicks = 45; // 450us
constexpr uint16_t kLongSpaceTicks = 130; // 1300us
// Section gap: shorter than the receiver's idle threshold (30ms default), so
// the whole frame still lands in one capture.
constexpr uint16_t kSectionGapTicks = 2000; // 20ms
constexpr uint32_t kSectionPulses = 100;    // ~130ms of burst per section
constexpr uint32_t kMaxPulses = 1000;

void sendReady()
{
  Serial.print("TX_READY gpio=");
  Serial.print(kIrTxGpio);
  Serial.print(" inverted=");
  Serial.println(kIrTxInverted ? 1 : 0);
}

bool readLine(String &line)
{
  if (!Serial.available())
  {
    return false;
  }
  line = Serial.readStringUntil('\n');
  line.trim();
  return line.length() > 0;
}

// <pulses> marks separated by <pulses - 1> spaces: 2 * pulses - 1 ticks, which the
// receiver captures as exactly <pulses> RMT symbols.
bool sendPulses(uint32_t pulses)
{
  std::vector<uint16_t> ticks;
  ticks.reserve(pulses * 2);
  for (uint32_t i = 0; i < pulses; ++i)
  {
    if (i > 0)
    {
      ticks.push_back((i % kSectionPulses) == 0 ? kSectionGapTicks
                      : (i % 2)                 ? kShortSpaceTicks
                                                : kLongSpaceTicks);
    }
    ticks.push_back(kMarkTicks);
  }
  esp32irpk::IRRawTickView raw{};
  raw.ticks = ticks.data();
  raw.len = ticks.size();
  return tx.send(raw, 0);
}

// SEND PULSES <n>
void handleSendPulses(const String &line)
{
  String text = line.substring(String("SEND PULSES ").length());
  text.trim();
  char *end = nullptr;
  unsigned long pulses = strtoul(text.c_str(), &end, 10);
  if (text.length() == 0 || *end != '\0' || pulses == 0 || pulses > kMaxPulses)
  {
    Serial.println("TX_ERROR invalid_pulses");
    return;
  }
  if (!sendPulses(pulses))
  {
    Serial.println("TX_ERROR send_failed");
    return;
  }
  Serial.print("TX_OK PULSES ");
  Serial.println(pulses);
}
} // namespace

void setup()
{
  Serial.begin(115200);
  delay(5000);
  if (!tx.begin())
  {
    Serial.println("TX_ERROR begin_failed");
    return;
  }
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
    else if (line.startsWith("SEND PULSES "))
    {
      handleSendPulses(line);
    }
    else
    {
      Serial.print("TX_ERROR unknown_command ");
      Serial.println(line);
    }
  }
  delay(1);
}
