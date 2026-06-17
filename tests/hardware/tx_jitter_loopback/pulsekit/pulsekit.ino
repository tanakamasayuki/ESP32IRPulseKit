// Single-board carrier-off loopback jitter rig (TX: ESP32IRPulseKit).
//
// One board does everything: it transmits NEC on IR_TX_GPIO with the carrier
// DISABLED (solid marks), and captures the wire-looped signal on IR_RX_GPIO
// with the RMT peripheral at 1 MHz (1 us/tick). Wire IR_TX_GPIO -> IR_RX_GPIO
// directly (no IR LED / no receiver module), so there is no carrier modulation
// and no TSOP demodulation distortion. TX and RX share one clock, so the spread
// across repeats reflects pure transmit timing jitter.
#include <ESP32IRPulseKit.h>

// Loopback-specific GPIO macros (distinct from IR_TX_GPIO/IR_RX_GPIO used by the
// other rigs and mapped from .env). Fixed to the wired pair on one board.
#ifndef LOOPBACK_TX_GPIO
#define LOOPBACK_TX_GPIO "5"
#endif
#ifndef LOOPBACK_RX_GPIO
#define LOOPBACK_RX_GPIO "6"
#endif

const int kIrTxGpio = atoi(LOOPBACK_TX_GPIO);
const int kIrRxGpio = atoi(LOOPBACK_RX_GPIO);

esp32irpk::IRSender tx(kIrTxGpio, false);

// 1 MHz RMT capture (1 us/tick). Idle threshold ends a frame; must exceed the
// longest valid space and stay within the 15-bit duration field.
const uint32_t kRmtFreqHz = 1000000;
const uint16_t kIdleThresholdUs = 10000;
const size_t kCap = 128;
rmt_data_t g_rxbuf[kCap];
size_t g_rxNum = kCap;
uint32_t g_seq = 0;
bool g_rxReady = false;

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

  bool parseHex64(const String &text, uint64_t &value)
  {
    if (text.length() == 0)
      return false;
    char *end = nullptr;
    value = strtoull(text.c_str(), &end, 16);
    return end != text.c_str() && *end == '\0';
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
    Serial.print("RX_READY impl=ESP32IRPulseKit-loopback tx_gpio=");
    Serial.print(kIrTxGpio);
    Serial.print(" rx_gpio=");
    Serial.print(kIrRxGpio);
    Serial.println(" carrier=off resolution_us=1");
  }

  bool sendBits(esp32irpk::IRProtocolID protocol, uint16_t bitLength, uint64_t bits)
  {
    esp32irpk::IRDecodedBits decoded{};
    decoded.protocol_id = protocol;
    decoded.frame_type = esp32irpk::IRFrameType::NORMAL;
    decoded.bit_length = bitLength;
    decoded.bits = bits;
    return tx.send(decoded, 0);
  }

  void handleSend(const String &line)
  {
    int pos = String("SEND").length();
    String protocolText, bitsText;
    if (!nextToken(line, pos, protocolText) || !nextToken(line, pos, bitsText))
    {
      Serial.println("TX_ERROR invalid_send_command");
      return;
    }
    uint64_t bits = 0;
    if (!parseHex64(bitsText, bits))
    {
      Serial.println("TX_ERROR invalid_bits");
      return;
    }

    bool ok = false;
    if (protocolText == "NEC")
      ok = sendBits(esp32irpk::IRProtocolID::NEC, 32, bits);
    else if (protocolText == "SONY12")
      ok = sendBits(esp32irpk::IRProtocolID::SONY12, 12, bits);
    else if (protocolText == "SAMSUNG32")
      ok = sendBits(esp32irpk::IRProtocolID::SAMSUNG32, 32, bits);
    else if (protocolText == "JVC24")
      ok = sendBits(esp32irpk::IRProtocolID::JVC24, 24, bits);
    else
    {
      Serial.println("TX_ERROR unsupported_protocol");
      return;
    }
    if (!ok)
    {
      Serial.println("TX_ERROR send_failed");
      return;
    }
    Serial.print("TX_OK ");
    Serial.print(protocolText);
    Serial.print(" ");
    Serial.println(bitsText);
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
    while (m > 0 && durs[m - 1] == 0)
      m--;
    // Emit one logical line, but paced: the ~400-byte RX_JITTER line is too long
    // to push over the 115200 USB-UART bridge in one continuous burst without the
    // host/bridge receive buffer occasionally overrunning and dropping a few
    // bytes mid-line. Flush in small chunks with a brief inter-chunk gap so the
    // bridge FIFO never backs up. flush() blocks until the ESP TX FIFO drains
    // (paces to baud); the delay gives the host time to drain the bridge.
    Serial.print("RX_JITTER seq=");
    Serial.print((unsigned long)g_seq++);
    Serial.print(" len=");
    Serial.print((unsigned long)m);
    Serial.print(" us=");
    Serial.flush();
    delay(3);
    for (size_t i = 0; i < m; ++i)
    {
      if (i > 0)
        Serial.print(',');
      Serial.print((unsigned long)durs[i]);
      if ((i & 0x0F) == 0x0F)
      {
        Serial.flush();
        delay(3);
      }
    }
    Serial.println();
    Serial.flush();
    // Let the long line fully ship over USB before the loop re-arms RMT, so its
    // tail (newline + last values) is not occasionally dropped.
    delay(3);
  }
} // namespace

void setup()
{
  Serial.setTxBufferSize(1024); // headroom for the long RX_JITTER lines
  Serial.begin(115200);
  delay(5000);

  if (!tx.begin())
  {
    Serial.println("TX_ERROR begin_failed");
    return;
  }
  tx.disableCarrier();

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
    else if (line.startsWith("SEND "))
      handleSend(line);
  }

  if (g_rxReady && rmtReceiveCompleted(kIrRxGpio))
  {
    if (g_rxNum > 0)
      dumpFrame();
    armRead();
  }

  delay(1);
}
