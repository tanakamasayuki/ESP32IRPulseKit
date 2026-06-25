#include <ESP32IRPulseKit.h>

// en: Build Panasonic AC frames from scratch and send them — no remote needed.
//     Shows the construct -> encode -> send path with a few real operations.
//     Adjust the GPIO number to your board wiring.
// ja: PanasonicのACフレームをゼロから組み立てて送信する例（リモコン不要）。
//     構築 -> encode -> 送信の流れを、いくつかの実操作で示します。
//     GPIO番号はご利用環境の配線に合わせて変更してください。
esp32irpk::IRSender tx(4);

void setup()
{
  Serial.begin(115200);
  // en: AC uses the phase-aligned carrier (the default — do not call
  //     setPhaseAlignedCarrier). It is the safe choice across vendors; the
  //     hardware carrier (setPhaseAlignedCarrier(false)) saves memory but some
  //     vendors (e.g. Gree) drop frames on it. Panasonic tolerates either.
  // ja: ACは位相整合キャリア（既定。setPhaseAlignedCarrier を呼ばない）を使う。
  //     ベンダ横断で安全。ハードウェアキャリア（setPhaseAlignedCarrier(false)）は
  //     省メモリだが一部ベンダ（例: Gree）はフレームが落ちる。Panasonicは両対応。
  tx.begin();
}

void loop()
{
  // en: Start from an empty frame and configure it. toRaw() computes the
  //     checksum, so you only set the logical fields.
  // ja: 空のフレームから設定する。チェックサムは toRaw() が計算するので、論理
  //     フィールドを設定するだけでよい。
  esp32irpk::ac::Panasonic::Frame f;
  f.setPower(true);
  f.setMode(esp32irpk::ac::Panasonic::Mode::COOL);
  f.setFan(esp32irpk::ac::Panasonic::Fan::AUTO);

  // en: Operation 1 — cool down step by step.
  // ja: 操作1 — 段階的に冷やす。
  for (uint8_t temp = 28; temp >= 26; --temp)
  {
    f.setTemperatureC(temp);
    Serial.print("cool ");
    Serial.print((unsigned)temp);
    Serial.println("C");
    // en: ac::send encodes the frame and transmits it in one call.
    // ja: ac::send はフレームをエンコードして1呼び出しで送信する。
    Serial.println(esp32irpk::ac::send(tx, f) ? "  sent" : "  send failed");
    delay(4000);
  }

  // en: Operation 2 — turn the unit off.
  // ja: 操作2 — 停止する。
  f.setPower(false);
  Serial.println("power off");
  Serial.println(esp32irpk::ac::send(tx, f) ? "  sent" : "  send failed");
  delay(8000);
}
