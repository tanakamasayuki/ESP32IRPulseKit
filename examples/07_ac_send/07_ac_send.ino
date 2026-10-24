// en: Build and transmit an AC state
// ja: エアコンの状態を組み立てて送信する
#include <ESP32IRPulseKit.h>

// en: This GPIO drives an IR LED driver; see README for the wiring requirements.
// ja: このGPIOで赤外線LEDの駆動回路を制御します。配線条件はREADMEを参照してください。
esp32irpk::IRSender tx(4);

void setup()
{
  Serial.begin(115200);
  tx.begin();
}

void loop()
{
  // en: An AC frame carries the full state, including power, mode and fan, not just temperature.
  // ja: ACフレームは温度だけでなく、電源・モード・風量を含む状態全体を運びます。
  esp32irpk::ac::Panasonic::Frame f;
  f.setPower(true);
  f.setMode(esp32irpk::ac::Panasonic::Mode::COOL);
  f.setFan(esp32irpk::ac::Panasonic::Fan::AUTO);

  for (uint8_t temp = 28; temp >= 26; --temp)
  {
    f.setTemperatureC(temp);
    Serial.print("cool ");
    Serial.print((unsigned)temp);
    Serial.println("C");
    // en: Encode the state and wait for local transmission; success is not an appliance acknowledgement.
    // ja: 状態を変換してローカル送信完了を待ちます。成功は家電からの受信応答ではありません。
    Serial.println(esp32irpk::ac::send(tx, f) ? "  sent" : "  send failed");
    delay(4000);
  }

  f.setPower(false);
  Serial.println("power off");
  Serial.println(esp32irpk::ac::send(tx, f) ? "  sent" : "  send failed");
  delay(8000);
}
