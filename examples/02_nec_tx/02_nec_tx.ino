#include <ESP32IRPulseKit.h>

// en: Simple NEC transmitter example.
// ja: NEC信号を送信する簡単な例です。
// en: Adjust GPIO numbers to your board wiring.
// ja: GPIO番号はご利用環境の配線に合わせて変更してください。
esp32irpk::IRSender tx(4);

void setup()
{
  Serial.begin(115200);
  if (!tx.begin())
  {
    Serial.println("TX begin failed");
  }
}

void loop()
{
  esp32irpk::frames::NECFrame frame{};
  frame.address = 0x00ff;
  frame.command = 0x34;

  if (tx.send(frame.toBits()))
  {
    Serial.println("NEC sent");
  }
  else
  {
    Serial.println("NEC send failed");
  }

  delay(1000);
}
