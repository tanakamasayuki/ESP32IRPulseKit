#include <ESP32IRPulseKit.h>

// en: Send one frame per built-in protocol in a loop.
// ja: 内蔵プロトコルを1フレームずつ順番に送信する例です。
// en: Adjust the GPIO number to your board wiring.
// ja: GPIO番号はご利用環境の配線に合わせて変更してください。
esp32irpk::IRSender tx(4);

// en: The sender tops up every built-in protocol at begin(), so send(bits)
//     resolves each protocol's preferred carrier automatically (e.g. Sony 40kHz,
//     RC5 36kHz). No per-protocol setCarrierHz() is needed.
// ja: begin()で全内蔵プロトコルが補完されるため、send(bits)は各プロトコルの
//     推奨キャリア（例: Sony 40kHz, RC5 36kHz）を自動で使います。個別の
//     setCarrierHz()は不要です。

void setup()
{
  Serial.begin(115200);
  if (!tx.begin())
  {
    Serial.println("TX begin failed");
  }
}

static void report(const char *label, bool ok)
{
  Serial.print(label);
  Serial.println(ok ? " sent" : " send failed");
}

void loop()
{
  // en: Every protocol has a bits helper that returns IRDecodedBits.
  //     The Frame route (e.g. esp32irpk::frames::NECFrame{...}.toBits()) is
  //     equivalent when you want named logical fields.
  // ja: すべてのプロトコルにIRDecodedBitsを返すbitsヘルパーがあります。
  //     名前付きの論理フィールドが欲しいときはFrame経由
  //     （例: esp32irpk::frames::NECFrame{...}.toBits()）でも同じです。
  report("NEC", tx.send(esp32irpk::bits::nec(0x00ff, 0x34)));
  report("AEHA", tx.send(esp32irpk::bits::aeha(0x123456789abcULL, 48)));
  report("SONY12", tx.send(esp32irpk::bits::sony12(0x0a90)));
  report("SONY15", tx.send(esp32irpk::bits::sony15(0x2a90)));
  report("SONY20", tx.send(esp32irpk::bits::sony20(0x5a90)));
  report("SAMSUNG32", tx.send(esp32irpk::bits::samsung32(0xe0e0, 0x40bf)));
  report("SAMSUNG36", tx.send(esp32irpk::bits::samsung36(0x0707, 0x12345)));
  report("JVC", tx.send(esp32irpk::bits::jvc(0xc0, 0xde)));
  report("RC5", tx.send(esp32irpk::bits::rc5(0x300f)));
  report("RC6_M0_16", tx.send(esp32irpk::bits::rc6m0(0x11234)));
  report("RC6_M6_32", tx.send(esp32irpk::bits::rc6m6(0x689abcdefULL)));

  Serial.println();
  delay(2000);
}
