// en: Compare protocol-specific send helpers
// ja: プロトコル別の送信ヘルパーを試す
#include <ESP32IRPulseKit.h>

// en: This GPIO drives an IR LED driver; see README for the wiring requirements.
// ja: このGPIOで赤外線LEDの駆動回路を制御します。配線条件はREADMEを参照してください。
esp32irpk::IRSender tx(4);

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
  // en: Each helper builds protocol-specific bits; send() selects that protocol carrier automatically.
  // ja: 各ヘルパーがプロトコル固有のbitsを作り、send()が対応するキャリアを自動選択します。
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
