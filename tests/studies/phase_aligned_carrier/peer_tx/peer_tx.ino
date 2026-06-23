// ESP32IRPulseKit carrier-mode A/B transmitter.
//
// Drives a NEC-SHAPED RAW frame through the library's IRSender so we measure the
// REAL HAL carrier path (not a raw-RMT probe). The payload is irrelevant — the
// probe.ino RX captures at 1 us and the study compares demodulated mark
// stability between the two carrier methods:
//   - hw: free-running hardware carrier (rmt_apply_carrier), the default
//   - pa: phase-aligned, symbol-encoded carrier (setPhaseAlignedCarrier(true))
//
// Switching mode re-creates the TX channel (resolution differs), so a "SEND"
// with a new mode does end()+setPhaseAlignedCarrier()+begin() first.
//
// Command: "SEND <mode> <mark_us> <carrier_hz> <duty_pct>"
//   e.g. "SEND pa 560 38000 33"  /  "SEND hw 560 38000 33"
#include <Arduino.h>
#include <ESP32IRPulseKit.h>

#ifndef IR_TX_GPIO
#define IR_TX_GPIO "4"
#endif
#ifndef IR_TX_INVERTED
#define IR_TX_INVERTED "0"
#endif

const int kIrTxGpio = atoi(IR_TX_GPIO);
const bool kIrTxInverted = atoi(IR_TX_INVERTED) != 0;

esp32irpk::IRSender tx(kIrTxGpio, kIrTxInverted);
bool g_begun = false;
bool g_phaseAligned = false;

// NEC-shaped RAW in 10us library ticks (mark-first): header + 32 bits + trailer.
const uint16_t kHeaderMarkTicks = 900;  // 9000 us
const uint16_t kHeaderSpaceTicks = 450; // 4500 us
const uint16_t kZeroSpaceTicks = 56;    // 560 us
const uint16_t kOneSpaceTicks = 169;    // 1690 us
const uint32_t kPayload = 0xCB3400FF;    // arbitrary; only the marks are studied
uint16_t g_frame[2 + 64 + 1];
size_t g_frameLen = 0;

namespace
{
  bool readLine(String &line)
  {
    if (!Serial.available())
      return false;
    line = Serial.readStringUntil('\n');
    line.trim();
    return line.length() > 0;
  }

  bool nextToken(const String &line, int &pos, String &token)
  {
    while (pos < line.length() && line[pos] == ' ')
      pos++;
    if (pos >= line.length())
      return false;
    int end = line.indexOf(' ', pos);
    if (end < 0)
    {
      token = line.substring(pos);
      pos = line.length();
    }
    else
    {
      token = line.substring(pos, end);
      pos = end + 1;
    }
    return true;
  }

  void sendReady()
  {
    Serial.print("TX_READY impl=ESP32IRPulseKit gpio=");
    Serial.print(kIrTxGpio);
    Serial.print(" inverted=");
    Serial.print(kIrTxInverted ? 1 : 0);
    Serial.println(" resolution_us=1");
  }

  // (Re)open the sender in the requested carrier mode. The channel resolution is
  // fixed at begin(), so a mode change must end() then begin() again.
  bool ensureMode(bool phaseAligned)
  {
    if (g_begun && g_phaseAligned == phaseAligned)
      return true;
    if (g_begun)
    {
      tx.end();
      g_begun = false;
    }
    tx.setPhaseAlignedCarrier(phaseAligned);
    if (!tx.begin())
      return false;
    g_begun = true;
    g_phaseAligned = phaseAligned;
    return true;
  }

  void buildFrame(uint16_t markTicks)
  {
    size_t n = 0;
    g_frame[n++] = kHeaderMarkTicks;
    g_frame[n++] = kHeaderSpaceTicks;
    for (int b = 0; b < 32; ++b)
    {
      bool bit = (kPayload >> (31 - b)) & 0x1u;
      g_frame[n++] = markTicks;
      g_frame[n++] = bit ? kOneSpaceTicks : kZeroSpaceTicks;
    }
    g_frame[n++] = markTicks; // trailer mark (no trailing space)
    g_frameLen = n;
  }

  void handleSend(const String &line)
  {
    int pos = String("SEND").length();
    String modeText, markText, hzText, dutyText;
    if (!nextToken(line, pos, modeText) || !nextToken(line, pos, markText) ||
        !nextToken(line, pos, hzText) || !nextToken(line, pos, dutyText))
    {
      Serial.println("TX_ERROR invalid_send_command");
      return;
    }
    bool phaseAligned;
    if (modeText == "pa")
      phaseAligned = true;
    else if (modeText == "hw")
      phaseAligned = false;
    else
    {
      Serial.println("TX_ERROR invalid_mode");
      return;
    }
    uint32_t markUs = (uint32_t)markText.toInt();
    uint32_t carrierHz = (uint32_t)hzText.toInt();
    float dutyPct = dutyText.toFloat();
    if (markUs < 100 || markUs > 30000 || carrierHz < 1000 ||
        dutyPct <= 0.0f || dutyPct >= 100.0f)
    {
      Serial.println("TX_ERROR invalid_params");
      return;
    }

    if (!ensureMode(phaseAligned))
    {
      Serial.println("TX_ERROR begin_failed");
      return;
    }
    tx.setCarrierHz(carrierHz);
    tx.setCarrierDuty(dutyPct / 100.0f);
    buildFrame((uint16_t)(markUs / 10));

    esp32irpk::IRRawTickView view{g_frame, g_frameLen};
    if (!tx.send(view, 0))
    {
      Serial.println("TX_ERROR send_failed");
      return;
    }
    Serial.print("TX_OK mode=");
    Serial.print(phaseAligned ? "pa" : "hw");
    Serial.print(" mark=");
    Serial.print(markUs);
    Serial.print(" hz=");
    Serial.print(carrierHz);
    Serial.print(" duty=");
    Serial.println(dutyPct, 2);
  }

  void handleCommand(const String &line)
  {
    if (line == "PING")
    {
      Serial.println("PONG");
      return;
    }
    if (line == "READY")
    {
      sendReady();
      return;
    }
    if (line.startsWith("SEND "))
    {
      handleSend(line);
      return;
    }
    Serial.print("TX_ERROR unknown_command ");
    Serial.println(line);
  }
} // namespace

void setup()
{
  Serial.begin(115200);
  delay(5000);
  ensureMode(false); // start in hardware-carrier mode
  sendReady();
}

void loop()
{
  String line;
  if (readLine(line))
    handleCommand(line);
  delay(1);
}
