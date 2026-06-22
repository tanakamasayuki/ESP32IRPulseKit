// JVC timing-sweep peer TX (ESP32IRPulseKit). Emits a JVC-shaped frame with
// tunable bit-mark / zero-space / one-space so the host can sweep the timing and
// see which values keep the received zero-space inside IRremoteESP8266's window.
//
// Command: JVCRAW <mark_us> <zero_space_us> <one_space_us> <hex_bits>
//   Header is fixed at the JVC standard 8400/4200; 16 data bits LSB-first;
//   trailer mark = bit-mark. Carrier stays at the library default (38 kHz/0.33).
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

const size_t kMaxTicks = 2 + 32 + 2;
uint16_t g_ticks[kMaxTicks];

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

uint16_t usToTicks(uint32_t us) { return static_cast<uint16_t>((us + 5) / 10); }

void sendReady()
{
  Serial.print("TX_READY impl=ESP32IRPulseKit gpio=");
  Serial.print(kIrTxGpio);
  Serial.print(" inverted=");
  Serial.println(kIrTxInverted ? 1 : 0);
}

void handleJvcRaw(const String &line)
{
  int pos = String("JVCRAW").length();
  String a, b, c, d;
  if (!nextToken(line, pos, a) || !nextToken(line, pos, b) ||
      !nextToken(line, pos, c) || !nextToken(line, pos, d))
  {
    Serial.println("JVCRAW_ERROR usage: JVCRAW <mark_us> <zero_space_us> <one_space_us> <hex_bits>");
    return;
  }
  uint16_t mark_t = usToTicks(a.toInt());
  uint16_t zsp_t = usToTicks(b.toInt());
  uint16_t osp_t = usToTicks(c.toInt());
  char *end = nullptr;
  uint64_t bits = strtoull(d.c_str(), &end, 16);
  if (mark_t == 0 || zsp_t == 0 || osp_t == 0)
  {
    Serial.println("JVCRAW_ERROR bad_timing");
    return;
  }

  size_t n = 0;
  g_ticks[n++] = usToTicks(8400); // header mark (JVC standard, fixed)
  g_ticks[n++] = usToTicks(4200); // header space
  for (int i = 0; i < 16; ++i)
  {
    bool bit = ((bits >> i) & 0x1ULL) != 0; // LSB-first
    g_ticks[n++] = mark_t;
    g_ticks[n++] = bit ? osp_t : zsp_t;
  }
  g_ticks[n++] = mark_t; // trailer mark

  esp32irpk::IRRawTickView view{};
  view.ticks = g_ticks;
  view.len = n;
  if (!tx.send(view, 0))
  {
    Serial.println("JVCRAW_ERROR send_failed");
    return;
  }
  Serial.print("JVCRAW_OK mark=");
  Serial.print(a);
  Serial.print(" zspace=");
  Serial.print(b);
  Serial.print(" ospace=");
  Serial.print(c);
  Serial.print(" bits=");
  Serial.println(d);
}

void handleCommand(const String &line)
{
  if (line == "PING")
    Serial.println("PONG");
  else if (line == "READY")
    sendReady();
  else if (line.startsWith("JVCRAW "))
    handleJvcRaw(line);
  else
  {
    Serial.print("TX_ERROR unknown_command ");
    Serial.println(line);
  }
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
    handleCommand(line);
  delay(1);
}
