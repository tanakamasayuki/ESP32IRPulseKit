// High-resolution RMT receiver for the carrier/mark-width jitter probe.
//
// Identical capture path to studies/tx_jitter/: drive the ESP32 RMT peripheral
// directly at 1 MHz (1 us/tick) and dump every captured edge duration in
// microseconds (`RX_JITTER seq=.. len=.. us=..`). The peer (peer_tx/) transmits
// a NEC-shaped frame with the RMT carrier, sweeping mark width / carrier
// frequency / duty; this side measures how stable the demodulated edges are.
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
bool g_prime = true; // drop the first captured frame (priming; see dumpFrame)

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
    // Re-arm immediately (before the paced serial emit below) so the next frame
    // is not missed during the tens of ms the long line takes to ship. The TX is
    // a separate board, triggered by the test the moment it sees this line, so an
    // unarmed window during the emit would drop the next capture.
    armRead();
    // Skip spurious empty captures (e.g. a startup transient) so they don't emit
    // a len=0 line.
    if (m == 0)
      return;
    // Prime: drop the first real frame (no line emitted) so each measurement
    // point starts from a steady state.
    if (g_prime)
    {
      g_prime = false;
      return;
    }
    // Emit one logical line, paced: the ~400-byte line is too long to push over
    // the 115200 USB-UART bridge in one continuous burst without the host/bridge
    // receive buffer occasionally overrunning and dropping bytes mid-line. Flush
    // in small chunks with a brief inter-chunk gap so the bridge FIFO never backs
    // up.
    Serial.print("RX_JITTER seq=");
    Serial.print(g_seq++);
    Serial.print(" len=");
    Serial.print(m);
    Serial.print(" us=");
    Serial.flush();
    delay(20);
    for (size_t i = 0; i < m; ++i)
    {
      if (i > 0)
        Serial.print(",");
      Serial.print(durs[i]);
      if ((i & 0x0F) == 0x0F)
      {
        Serial.flush();
        delay(20);
      }
    }
    Serial.println();
    Serial.flush();
    delay(20);
  }
} // namespace

void setup()
{
  Serial.setTxBufferSize(1024); // headroom for the long RX_JITTER lines
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
      dumpFrame(); // copies out, re-arms, then emits
    else
      armRead();
  }

  delay(1);
}
