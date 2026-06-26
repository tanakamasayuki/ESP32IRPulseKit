#include <ESP32IRPulseKit.h>
#include <IRDebug.h> // en: optional Serial-formatting helpers / ja: 任意のシリアル整形ヘルパー

// en: Simple RX example that prints decoded candidates to Serial.
// ja: 受信した信号をデコードしてシリアルに出力する簡単な例。
// en: Adjust GPIO numbers to your board wiring.
// ja: GPIO番号はご利用環境の配線に合わせて変更してください。
esp32irpk::IRReceiver rx(32, true); // en: common IR receiver modules output inverted / ja: よく使われる受信モジュールは出力が反転

void setup()
{
  Serial.begin(115200);
  // rx.setScoreThreshold(-32768); // en: show negative-score candidates / ja: 負スコア候補も表示する
  rx.begin();
}

void loop()
{
  esp32irpk::IRReceiveResult r;
  if (!rx.read(r))
  {
    delay(1);
    return;
  }

  Serial.println("---- IR received ----");
  Serial.print("raw.len(ticks)=");
  Serial.print((unsigned)r.raw.len);
  Serial.print(" flags=0x");
  Serial.println((unsigned)r.flags, HEX);
  esp32irpk::debug::printRawMicros(Serial, r.raw);

  if (r.count == 0)
  {
    Serial.println("no decoded candidates");
    Serial.println();
    return;
  }

  // en: one line per candidate + its named-field decode (see <IRDebug.h>).
  // ja: 候補ごとに1行＋名前付きフィールドデコード（<IRDebug.h> 参照）。
  for (uint8_t i = 0; i < r.count; ++i)
  {
    const esp32irpk::IRDecodeCandidate &c = r.candidates[i];
    esp32irpk::debug::printDecodedCandidate(Serial, i, c);
    esp32irpk::debug::printDecodedFrame(Serial, c.decoded);
    Serial.println();
  }
}
