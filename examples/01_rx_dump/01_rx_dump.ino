// en: Receive and inspect protocol candidates
// ja: 受信波形とプロトコル候補を調べる
#include <ESP32IRPulseKit.h>
// en: Optional Serial-formatting and generated-code helpers.
// ja: シリアル表示とコード生成用の任意ヘルパーです。
#include <IRDebug.h>

// en: Match this GPIO and input polarity to the receiver module OUT signal.
// ja: GPIOと入力極性を受信モジュールのOUT信号に合わせます。
esp32irpk::IRReceiver rx(32, true);

void setup()
{
  Serial.begin(115200);
  // rx.setScoreThreshold(-32768); // en: show negative-score candidates / ja: 負スコア候補も表示する
  // en: Start capture after applying settings; check the bool result when adapting this example.
  // ja: 設定後に受信を開始します。組み込む際はboolの戻り値も確認します。
  rx.begin();
}

void loop()
{
  esp32irpk::IRReceiveResult r;
  // en: No completed capture is available yet; yield briefly and try again on the next loop.
  // ja: 受信結果がまだない場合は少し待ち、次のloopで再度確認します。
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
  // en: Format borrowed RAW timings now, before the next read can replace the receiver buffer.
  // ja: 次のreadでバッファが置き換わる前に、参照中のRAW時間列を表示します。
  esp32irpk::debug::printRawMicros(Serial, r.raw);

  if (r.count == 0)
  {
    Serial.println("no decoded candidates");
    Serial.println();
    return;
  }

  // en: Inspect every candidate; scores rank possible decodes rather than certifying the protocol.
  // ja: 全候補を確認します。スコアは候補の順位付けで、プロトコルの確定保証ではありません。
  for (uint8_t i = 0; i < r.count; ++i)
  {
    const esp32irpk::IRDecodeCandidate &c = r.candidates[i];
    esp32irpk::debug::printDecodedCandidate(Serial, i, c);
    esp32irpk::debug::printDecodedFrame(Serial, c.decoded);
    Serial.println();
  }
}
