#include <ESP32IRPulseKit.h>
#include <IRDebug.h> // en: optional Serial-formatting helpers / ja: 任意のシリアル整形ヘルパー

// en: RAW-only capture plus receive statistics. No protocol decode runs.
// ja: RAWのみのキャプチャと受信統計。プロトコルデコードは行いません。
// en: Adjust the GPIO number to your board wiring.
// ja: GPIO番号はご利用環境の配線に合わせて変更してください。
esp32irpk::IRReceiver rx(32, true);

void setup()
{
  Serial.begin(115200);
  // en: 0 candidates selects RAW-only mode: read() returns RAW with
  //     DECODE_SKIPPED and registers no built-in protocols.
  // ja: 候補数0でRAWのみモードになります。read()はDECODE_SKIPPED付きでRAWを返し、
  //     内蔵プロトコルは登録されません。
  rx.setDecodeCandidates(0);
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

  Serial.print("raw.len(ticks)=");
  Serial.print((unsigned)r.raw.len);
  Serial.print(" truncated=");
  Serial.print(r.truncated());
  Serial.print(" rmt_overflow=");
  Serial.println(r.rmtOverflow());

  esp32irpk::debug::printRawMicros(Serial, r.raw);

  // en: Cumulative receive health counters. Watch these for dropped data.
  // ja: 受信の健全性を示す累積カウンタ。データ欠落の監視に使えます。
  esp32irpk::IRRxStats s = rx.stats();
  Serial.print("stats: queue_overflow=");
  Serial.print((unsigned)s.queue_overflow_count);
  Serial.print(" rmt_overflow=");
  Serial.print((unsigned)s.rmt_overflow_count);
  Serial.print(" raw_truncated=");
  Serial.println((unsigned)s.raw_truncated_count);
  Serial.println();
}
