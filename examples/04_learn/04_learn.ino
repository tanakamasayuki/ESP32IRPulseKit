#include <ESP32IRPulseKit.h>
#include <IRDebug.h> // en: optional Serial-formatting helpers / ja: 任意のシリアル整形ヘルパー

// en: Learn a remote: receive a signal and print copy-paste C++ to re-send it.
//     RX only, so it runs on a single board (TX is usually a separate device).
// ja: リモコンの学習: 信号を受信し、再送するためのC++コードを貼り付け可能な形で
//     出力します。受信のみなので1台で動きます（送信側は別デバイスが普通）。
// en: Adjust the GPIO number to your board wiring.
// ja: GPIO番号はご利用環境の配線に合わせて変更してください。
esp32irpk::IRReceiver rx(32, true); // en: most receiver modules output inverted / ja: 多くの受信モジュールは出力反転

void setup()
{
  Serial.begin(115200);
  rx.begin();
}

void loop()
{
  esp32irpk::IRReceiveResult<> r;
  if (!rx.read(r))
  {
    delay(1);
    return;
  }

  Serial.println("---- learned ----");
  // en: a decoded protocol re-sends as IRDecodedBits; an unknown one as RAW.
  //     Both snippets are produced by <IRDebug.h>.
  // ja: デコードできたものは IRDecodedBits、不明なものは RAW で再送する。どちらの
  //     スニペットも <IRDebug.h> が生成する。
  if (const esp32irpk::IRDecodeCandidate *c = r.candidate())
  {
    Serial.print("protocol=");
    Serial.print(c->name);
    Serial.print(" score=");
    Serial.println((int)c->score);
    esp32irpk::debug::printBitsSendSnippet(Serial, *c);
  }
  else
  {
    Serial.println("protocol=unknown");
    esp32irpk::debug::printRawSendSnippet(Serial, r.raw);
  }
  Serial.println();
}
