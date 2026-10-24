// en: Send an NEC command
// ja: NECコマンドを送信する
#include <ESP32IRPulseKit.h>

// en: This GPIO drives an IR LED driver; see README for the wiring requirements.
// ja: このGPIOで赤外線LEDの駆動回路を制御します。配線条件はREADMEを参照してください。
esp32irpk::IRSender tx(4);

void setup()
{
  Serial.begin(115200);
  // tx.setCarrierHz(38000); // en: Optional fixed carrier. ja: 必要ならcarrierを明示固定できます。
  if (!tx.begin())
  {
    Serial.println("TX begin failed");
  }
}

void loop()
{
  // en: Replace the demo address and command with values observed from your own remote.
  // ja: 例のアドレスとコマンドを、実際のリモコンで調べた値へ置き換えます。
  esp32irpk::IRDecodedBits bits = esp32irpk::bits::nec(0x00ff, 0x34);

  // esp32irpk::frames::NECFrame frame{};
  // frame.address = 0x00ff;
  // frame.command = 0x34;
  // esp32irpk::IRDecodedBits bits = frame.toBits();

  // en: Wait for local send completion; the result does not acknowledge appliance reception.
  // ja: ローカル送信完了を待ちます。結果は家電の受信確認ではありません。
  if (tx.send(bits))
  {
    Serial.println("NEC sent");
  }
  else
  {
    Serial.println("NEC send failed");
  }

  delay(1000);
}
