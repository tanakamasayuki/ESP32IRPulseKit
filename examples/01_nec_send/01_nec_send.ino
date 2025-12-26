#include <ESP32IRPulseKit.h>

// en: Simple NEC TX example
// ja: NEC送信のシンプルな例
// en: Adjust GPIO numbers to your board wiring.
// ja: GPIO番号はご利用環境の配線に合わせて変更してください。
esp32irpk::IRSender tx(25);

void setup()
{
  Serial.begin(115200);
  tx.begin();
}

void loop()
{
  delay(1);
}
