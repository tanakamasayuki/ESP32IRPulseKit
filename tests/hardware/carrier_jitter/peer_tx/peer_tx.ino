// Raw-RMT carrier / mark-width sweep transmitter.
//
// Drives the IR LED through the ESP32 RMT peripheral directly at 1 MHz
// (1 us/tick), with the hardware carrier modulator (rmtSetCarrier). It builds a
// NEC-SHAPED frame (9000/4500 header, 32 bit-marks, stop mark) but the payload
// is irrelevant — the experiment varies the bit-mark WIDTH, the carrier
// FREQUENCY, and the carrier DUTY, then the probe.ino RX measures how stable the
// demodulated edges are across repeats.
//
// Why: a 560 us mark is 560 / (1e6/38000) = 21.28 carrier periods at 38 kHz, so
// the mark ends mid-cycle and the TSOP's last-pulse demodulation point wanders
// frame-to-frame. A width near an integer number of periods (21 * 26.316 us ~=
// 552.6 us) should end on a cycle boundary and demodulate more consistently.
//
// Command: "SEND <mark_us> <carrier_hz> <duty_pct>"  e.g. "SEND 560 38000 33"
//   mark_us    : width of every bit-mark and the stop mark (header stays 9000)
//   carrier_hz : carrier frequency in Hz
//   duty_pct   : carrier duty cycle in percent (rmtSetCarrier wants 0..1, so
//                this is divided by 100; ~20..50 is the useful range)
#include <Arduino.h>

#ifndef IR_TX_GPIO
#define IR_TX_GPIO "4"
#endif

const int kIrTxGpio = atoi(IR_TX_GPIO);

const uint32_t kRmtFreqHz = 1000000; // 1 tick = 1 us
const uint32_t kHeaderMarkUs = 9000;
const uint32_t kHeaderSpaceUs = 4500;
const uint32_t kZeroSpaceUs = 560;
const uint32_t kOneSpaceUs = 1690;
const uint32_t kPayload = 0xCB3400FF; // arbitrary; only the marks are studied

// NEC shape: 1 header + 32 bits + 1 stop = 34 symbols.
const size_t kFrameSymbols = 1 + 32 + 1;
rmt_data_t g_frame[kFrameSymbols];

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
    Serial.print("TX_READY impl=RMT-raw gpio=");
    Serial.print(kIrTxGpio);
    Serial.println(" resolution_us=1");
  }

  void buildFrame(uint32_t markUs)
  {
    size_t n = 0;
    g_frame[n].duration0 = kHeaderMarkUs;
    g_frame[n].level0 = 1;
    g_frame[n].duration1 = kHeaderSpaceUs;
    g_frame[n].level1 = 0;
    n++;
    for (int b = 0; b < 32; ++b)
    {
      bool bit = (kPayload >> (31 - b)) & 0x1u;
      g_frame[n].duration0 = markUs;
      g_frame[n].level0 = 1;
      g_frame[n].duration1 = bit ? kOneSpaceUs : kZeroSpaceUs;
      g_frame[n].level1 = 0;
      n++;
    }
    // Stop mark, terminated by a zero-duration trailing slot so the RX records
    // exactly the stop edge (like a real NEC frame: 2 + 64 + 1 = 67 edges) and
    // the line returns to idle low afterwards.
    g_frame[n].duration0 = markUs;
    g_frame[n].level0 = 1;
    g_frame[n].duration1 = 0;
    g_frame[n].level1 = 0;
    n++;
  }

  void handleSend(const String &line)
  {
    int pos = String("SEND").length();
    String markText, hzText, dutyText;
    if (!nextToken(line, pos, markText) || !nextToken(line, pos, hzText) ||
        !nextToken(line, pos, dutyText))
    {
      Serial.println("TX_ERROR invalid_send_command");
      return;
    }
    uint32_t markUs = (uint32_t)markText.toInt();
    uint32_t carrierHz = (uint32_t)hzText.toInt();
    float dutyPct = dutyText.toFloat();
    if (markUs == 0 || markUs > 30000 || carrierHz < 1000 ||
        dutyPct <= 0.0f || dutyPct >= 100.0f)
    {
      Serial.println("TX_ERROR invalid_params");
      return;
    }

    // rmtSetCarrier(pin, enable, carrier_level, freq, duty):
    //  - duty is a 0..1 fraction (it resets to 0.5 if given > 1).
    //  - carrier_level maps to flags.polarity_active_low. We modulate the HIGH
    //    level (marks), so it must be false; passing true would put the carrier
    //    on the spaces and leave marks as solid IR (the TSOP then fragments the
    //    long solid header).
    if (!rmtSetCarrier(kIrTxGpio, true, false, carrierHz, dutyPct / 100.0f))
    {
      Serial.println("TX_ERROR set_carrier_failed");
      return;
    }
    buildFrame(markUs);
    if (!rmtWrite(kIrTxGpio, g_frame, kFrameSymbols, 200))
    {
      Serial.println("TX_ERROR write_failed");
      return;
    }
    Serial.print("TX_OK mark=");
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
  if (!rmtInit(kIrTxGpio, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_4, kRmtFreqHz))
  {
    Serial.println("TX_ERROR rmt_init_failed");
    return;
  }
  g_ready = true;
  sendReady();
}

void loop()
{
  String line;
  if (readLine(line))
    handleCommand(line);
  delay(1);
}
