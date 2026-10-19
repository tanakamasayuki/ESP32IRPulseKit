// Hardware RX capacity test -- RX side.
//
// Receives in RAW-only mode and reports each capture's length and truncated flag.
// The harness re-opens the receiver with a different setMaxRxSymbols() capacity via
// the CAP command, then has the peer send synthetic frames of a known pulse count to
// check that frames within the capacity arrive intact and longer frames are cut at the
// capacity with RAW_TRUNCATED set.
#include <ESP32IRPulseKit.h>

#ifndef IR_RX_GPIO
#define IR_RX_GPIO "32"
#endif

#ifndef IR_RX_INVERTED
#define IR_RX_INVERTED "1"
#endif

const int kIrRxGpio = atoi(IR_RX_GPIO);
const bool kIrRxInverted = atoi(IR_RX_INVERTED) != 0;

esp32irpk::IRReceiver<> rx(kIrRxGpio, kIrRxInverted);

namespace
{
void sendReady()
{
  Serial.print("RX_READY gpio=");
  Serial.print(kIrRxGpio);
  Serial.print(" inverted=");
  Serial.println(kIrRxInverted ? 1 : 0);
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

bool openReceiver(size_t max_rx_symbols)
{
  rx.end();
  rx.setDecodeCandidates(0);
  rx.setMaxRxSymbols(max_rx_symbols);
  return rx.begin();
}

// CAP <symbols>: re-open the receiver with the given capacity (0 = library default).
void handleCap(const String &line)
{
  String text = line.substring(String("CAP ").length());
  text.trim();
  char *end = nullptr;
  unsigned long symbols = strtoul(text.c_str(), &end, 10);
  if (text.length() == 0 || *end != '\0')
  {
    Serial.println("RX_ERROR invalid_cap");
    return;
  }
  if (!openReceiver(symbols))
  {
    Serial.println("RX_ERROR begin_failed");
    return;
  }
  Serial.print("RX_CAP ");
  Serial.println(symbols);
}
} // namespace

void setup()
{
  Serial.begin(115200);
  delay(5000);
  if (!openReceiver(0))
  {
    Serial.println("RX_ERROR begin_failed");
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
    else if (line.startsWith("CAP "))
    {
      handleCap(line);
    }
    else
    {
      Serial.print("RX_ERROR unknown_command ");
      Serial.println(line);
    }
  }

  esp32irpk::IRReceiveResult<> result;
  if (rx.read(result))
  {
    Serial.print("RX_RAW len=");
    Serial.print(result.raw.len);
    Serial.print(" truncated=");
    Serial.println(result.truncated() ? 1 : 0);
  }

  delay(1);
}
