// High-resolution RMT receiver for TX jitter measurement.
//
// This sketch does NOT use the ESP32IRPulseKit library RX (which is fixed at
// 10 us/tick). It drives the ESP32 RMT peripheral directly at 1 MHz (1 us/tick)
// and dumps every captured edge duration in microseconds, so the pytest harness
// can send a fixed frame (NEC) many times and compute per-edge timing
// statistics (mean / stdev / jitter) of whatever library is transmitting.
//
// RX is RMT-based and should itself be very stable; the variation observed
// across repeats is dominated by the transmitter implementation.
#include <Arduino.h>

#ifndef IR_RX_GPIO
#define IR_RX_GPIO "32"
#endif

const int kIrRxGpio = atoi(IR_RX_GPIO);

// 1 MHz RMT resolution => 1 tick = 1 us.
const uint32_t kRmtFreqHz = 1000000;
// End a frame after this much idle. Must exceed the longest valid space
// (NEC 1-bit space ~1690 us) and stay within the 15-bit duration field.
const uint16_t kIdleThresholdUs = 10000;

const size_t kCap = 128; // RMT symbols; NEC needs ~34, this is ample
rmt_data_t g_rxbuf[kCap];
size_t g_rxNum = kCap;
uint32_t g_seq = 0;
bool g_ready = false;

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

void sendReady()
{
  Serial.print("RX_READY impl=RMT-1us gpio=");
  Serial.print(kIrRxGpio);
  Serial.println(" resolution_us=1");
}

void armRead()
{
  g_rxNum = kCap;
  rmtReadAsync(kIrRxGpio, g_rxbuf, &g_rxNum);
}

void dumpFrame()
{
  uint32_t durs[kCap * 2];
  size_t m = 0;
  for (size_t i = 0; i < g_rxNum; ++i)
  {
    durs[m++] = g_rxbuf[i].duration0;
    durs[m++] = g_rxbuf[i].duration1;
  }
  // Drop the trailing zero duration(s) left by the idle terminator.
  while (m > 0 && durs[m - 1] == 0)
    m--;

  Serial.print("RX_JITTER seq=");
  Serial.print(g_seq++);
  Serial.print(" len=");
  Serial.print(m);
  Serial.print(" us=");
  for (size_t i = 0; i < m; ++i)
  {
    if (i > 0)
      Serial.print(",");
    Serial.print(durs[i]);
  }
  Serial.println();
}
} // namespace

void setup()
{
  Serial.begin(115200);
  delay(5000);

  if (!rmtInit(kIrRxGpio, RMT_RX_MODE, RMT_MEM_NUM_BLOCKS_4, kRmtFreqHz))
  {
    Serial.println("RX_ERROR rmt_init_failed");
    return;
  }
  rmtSetRxMaxThreshold(kIrRxGpio, kIdleThresholdUs);
  g_ready = true;
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
  }

  if (g_ready && rmtReceiveCompleted(kIrRxGpio))
  {
    if (g_rxNum > 0)
      dumpFrame();
    armRead();
  }

  delay(1);
}
