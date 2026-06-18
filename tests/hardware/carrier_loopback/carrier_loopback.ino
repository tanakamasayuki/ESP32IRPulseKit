// Single-board carrier-ON loopback probe (TX: ESP32IRPulseKit).
//
// Purpose: look directly at the *carrier* the library emits, with the TSOP
// removed from the path. One board transmits a short pattern of carrier-
// modulated marks on LOOPBACK_TX_GPIO and captures the wire-looped electrical
// signal on LOOPBACK_RX_GPIO with the RMT peripheral at 1 MHz (1 us/tick).
// Because the carrier is ON and there is no demodulator, every carrier half-
// cycle shows up as its own RMT edge, so the host can measure the carrier
// period (is it clean, or dithered by the 10 us TX resolution?) and the phase
// of the carrier at each mark boundary (does it drift mark-to-mark because the
// RMT carrier free-runs?). That isolates the two candidate TX-side causes of
// the JVC compat jitter: carrier-period quantization vs free-running phase.
//
// Wire LOOPBACK_TX_GPIO -> LOOPBACK_RX_GPIO directly (no IR LED / no TSOP).
//
// Command: CAP <mark_us> <space_us> <count> [duty_pct] [carrier_hz]
//   Sends `count` carrier-modulated marks of `mark_us`, separated by spaces of
//   `space_us`, then dumps the raw 1 us capture. Keep the pattern short: at
//   38 kHz a mark is ~1 carrier cycle per 26 us, and the RMT capture is bounded
//   by RMT_MEM_NUM_BLOCKS_4 (~192 symbols), so e.g. 4 x 530 us marks (~80
//   cycles) is safe; a full protocol frame with carrier would overflow.
#include <ESP32IRPulseKit.h>

#ifndef LOOPBACK_TX_GPIO
#define LOOPBACK_TX_GPIO "5"
#endif
#ifndef LOOPBACK_RX_GPIO
#define LOOPBACK_RX_GPIO "6"
#endif

const int kIrTxGpio = atoi(LOOPBACK_TX_GPIO);
const int kIrRxGpio = atoi(LOOPBACK_RX_GPIO);

esp32irpk::IRSender tx(kIrTxGpio, false);

// 1 MHz RMT capture (1 us/tick). Carrier ON, so far more edges than an envelope
// capture -- keep the pattern short to stay within the RMT memory blocks.
const uint32_t kRmtFreqHz = 1000000;
const uint16_t kIdleThresholdUs = 8000; // ends a frame after the trailing space
const size_t kCap = 512;
rmt_data_t g_rxbuf[kCap];
size_t g_rxNum = kCap;
uint32_t g_seq = 0;
bool g_rxReady = false;

// Encoded-pattern scratch (10 us ticks, library RAW unit). Sized for a short
// mark/space pattern plus headroom.
const size_t kMaxPatternTicks = 64;
uint16_t g_pattern[kMaxPatternTicks];

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
    Serial.print("RX_READY impl=ESP32IRPulseKit-carrier-loopback tx_gpio=");
    Serial.print(kIrTxGpio);
    Serial.print(" rx_gpio=");
    Serial.print(kIrRxGpio);
    Serial.println(" carrier=on resolution_us=1");
  }

  void armRead()
  {
    g_rxNum = kCap;
    rmtReadAsync(kIrRxGpio, g_rxbuf, &g_rxNum);
  }

  // Build and transmit `count` marks of mark_us separated by space_us using the
  // library TX (so we measure exactly what ESP32IRPulseKit emits). Returns false
  // on bad args / capacity.
  bool sendPattern(uint32_t mark_us, uint32_t space_us, uint32_t count)
  {
    if (count == 0)
      return false;
    // ticks are 10 us units (library RAW). Round to nearest tick.
    uint16_t mark_t = static_cast<uint16_t>((mark_us + 5) / 10);
    uint16_t space_t = static_cast<uint16_t>((space_us + 5) / 10);
    if (mark_t == 0)
      return false;
    size_t n = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
      if (n + 2 > kMaxPatternTicks)
        return false;
      g_pattern[n++] = mark_t;
      // last entry: keep a space too so the frame ends with a gap >= idle.
      g_pattern[n++] = space_t;
    }
    esp32irpk::IRRawTickView view{};
    view.ticks = g_pattern;
    view.len = n;
    return tx.send(view, 0);
  }

  void handleCap(const String &line)
  {
    int pos = String("CAP").length();
    String a, b, c, d, e;
    if (!nextToken(line, pos, a) || !nextToken(line, pos, b) || !nextToken(line, pos, c))
    {
      Serial.println("CAP_ERROR usage: CAP <mark_us> <space_us> <count> [duty_pct] [carrier_hz]");
      return;
    }
    uint32_t mark_us = a.toInt();
    uint32_t space_us = b.toInt();
    uint32_t count = c.toInt();
    if (nextToken(line, pos, d) && d.length())
    {
      float duty = d.toFloat() / 100.0f;
      if (!tx.setCarrierDuty(duty))
      {
        Serial.println("CAP_ERROR bad_duty");
        return;
      }
    }
    if (nextToken(line, pos, e) && e.length())
    {
      uint32_t hz = e.toInt();
      if (!tx.setCarrierHz(hz))
      {
        Serial.println("CAP_ERROR bad_carrier_hz");
        return;
      }
    }
    if (!sendPattern(mark_us, space_us, count))
    {
      Serial.println("CAP_ERROR send_failed");
      return;
    }
    Serial.print("CAP_OK mark=");
    Serial.print(mark_us);
    Serial.print(" space=");
    Serial.print(space_us);
    Serial.print(" count=");
    Serial.println(count);
  }

  void dumpFrame()
  {
    // Levels strictly alternate in an RMT capture; report the first level so the
    // host can reconstruct (level, duration) pairs. Highs are carrier-on pulses.
    uint8_t lvl0 = g_rxbuf[0].level0;
    uint32_t durs[kCap * 2];
    size_t m = 0;
    for (size_t i = 0; i < g_rxNum; ++i)
    {
      durs[m++] = g_rxbuf[i].duration0;
      durs[m++] = g_rxbuf[i].duration1;
    }
    while (m > 0 && durs[m - 1] == 0)
      m--;
    if (m == 0)
      return;
    // Paced chunked output: these lines can be long; flush in small chunks so
    // the 115200 USB-UART bridge never overruns mid-line.
    Serial.print("CARRIER_RAW seq=");
    Serial.print((unsigned long)g_seq++);
    Serial.print(" lvl0=");
    Serial.print((unsigned)lvl0);
    Serial.print(" len=");
    Serial.print((unsigned long)m);
    Serial.print(" us=");
    Serial.flush();
    delay(10);
    for (size_t i = 0; i < m; ++i)
    {
      if (i > 0)
        Serial.print(',');
      Serial.print((unsigned long)durs[i]);
      if ((i & 0x0F) == 0x0F)
      {
        Serial.flush();
        delay(10);
      }
    }
    Serial.println();
    Serial.flush();
    delay(10);
  }
} // namespace

void setup()
{
  Serial.setTxBufferSize(1024);
  Serial.begin(115200);
  delay(5000);

  if (!tx.begin())
  {
    Serial.println("TX_ERROR begin_failed");
    return;
  }
  // Carrier stays ON (library default 38 kHz / duty 0.33); CAP can override.

  if (!rmtInit(kIrRxGpio, RMT_RX_MODE, RMT_MEM_NUM_BLOCKS_4, kRmtFreqHz))
  {
    Serial.println("RX_ERROR rmt_init_failed");
    return;
  }
  rmtSetRxMaxThreshold(kIrRxGpio, kIdleThresholdUs);
  g_rxReady = true;
  armRead();
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
    else if (line.startsWith("CAP "))
      handleCap(line);
  }

  if (g_rxReady && rmtReceiveCompleted(kIrRxGpio))
  {
    if (g_rxNum > 0)
      dumpFrame();
    armRead();
  }

  delay(1);
}
