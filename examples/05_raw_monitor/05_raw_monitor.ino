// en: Inspect RAW reception and dropped captures
// ja: RAW受信と取りこぼしを調べる
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
  // en: Capture RAW only; a zero candidate count is intentional in this mode.
  // ja: RAWのみ受信します。このモードで候補数が0になるのは意図どおりです。
  rx.setDecodeCandidates(0);
  // en: Start capture after applying settings; check the bool result when adapting this example.
  // ja: 設定後に受信を開始します。組み込む際はboolの戻り値も確認します。
  rx.begin();
}

void loop()
{
  esp32irpk::IRReceiveResult<> r;
  // en: No completed capture is available yet; yield briefly and try again on the next loop.
  // ja: 受信結果がまだない場合は少し待ち、次のloopで再度確認します。
  if (!rx.read(r))
  {
    delay(1);
    return;
  }

  Serial.print("raw.len(ticks)=");
  Serial.print((unsigned)r.raw.len);
  Serial.print(" truncated=");
  Serial.print(r.truncated());
  Serial.print(" rmt_overflow=");
  Serial.println(r.rmtOverflow());

  // en: Format borrowed RAW timings now, before the next read can replace the receiver buffer.
  // ja: 次のreadでバッファが置き換わる前に、参照中のRAW時間列を表示します。
  esp32irpk::debug::printRawMicros(Serial, r.raw);

  // en: These totals accumulate across reads; compare changes to find dropped or incomplete captures.
  // ja: readをまたいで累積する回数です。増加を比べて受信の欠落や不完全な取得を調べます。
  esp32irpk::IRRxStats s = rx.stats();
  Serial.print("stats: queue_overflow=");
  Serial.print((unsigned)s.queue_overflow_count);
  Serial.print(" rmt_overflow=");
  Serial.print((unsigned)s.rmt_overflow_count);
  Serial.print(" raw_truncated=");
  Serial.println((unsigned)s.raw_truncated_count);
  Serial.println();
}
