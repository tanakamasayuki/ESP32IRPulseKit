#include <ESP32IRPulseKit.h>

// ===========================================================================
// DRAFT — usability review only. Targets the per-vendor AC encode API in
//         SPEC §11.2 (`esp32irpk::ac::Panasonic::Frame::toRaw`), NOT YET
//         IMPLEMENTED. The logical setters and the Panasonic::Mode /
//         Panasonic::Fan enums below are PROVISIONAL and will be finalized from
//         the Panasonic reverse engineering. Does not compile yet; excluded
//         from build tests.
// 草案 — 使い勝手レビュー専用。SPEC §11.2 のベンダ別ACエンコードAPI（未実装の
//        `esp32irpk::ac::Panasonic::Frame::toRaw`）が前提。下のセッタや
//        Panasonic::Mode / Panasonic::Fan enum は仮で、Panasonic解析後に確定する。
//        まだコンパイルできず、ビルドテストからも除外しています。
// ===========================================================================

// en: Build Panasonic AC frames from scratch and send them — no remote needed.
//     Shows the construct -> encode -> send path with a few real operations.
//     Adjust the GPIO number to your board wiring.
// ja: PanasonicのACフレームをゼロから組み立てて送信する例（リモコン不要）。
//     構築 -> encode -> 送信の流れを、いくつかの実操作で示します。
//     GPIO番号はご利用環境の配線に合わせて変更してください。
esp32irpk::IRSender tx(4);

// en: Scratch buffer for the encoded frame. AC frames are long.
// ja: エンコード結果用のバッファ。ACフレームは長い。
static uint16_t out_ticks[2048];

static bool sendFrame(const esp32irpk::ac::Panasonic::Frame &f)
{
  esp32irpk::IRRawTickBuffer out{out_ticks, sizeof(out_ticks) / sizeof(out_ticks[0]), 0};
  if (!f.toRaw(out)) // en: fills bytes' checksum + renders ticks / ja: チェックサム込みでtick生成
  {
    Serial.println("encode failed (buffer too small?)");
    return false;
  }
  esp32irpk::IRRawTickView view{out.ticks, out.len};
  return tx.send(view);
}

void setup()
{
  Serial.begin(115200);
  // en: Long frames: hardware carrier keeps the RMT symbol count manageable.
  // ja: 長いフレームはハードウェアキャリアでRMTシンボル数を抑える。
  tx.setPhaseAlignedCarrier(false);
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
    Serial.println(sendFrame(f) ? "  sent" : "  send failed");
    delay(4000);
  }

  // en: Operation 2 — turn the unit off.
  // ja: 操作2 — 停止する。
  f.setPower(false);
  Serial.println("power off");
  Serial.println(sendFrame(f) ? "  sent" : "  send failed");
  delay(8000);
}
