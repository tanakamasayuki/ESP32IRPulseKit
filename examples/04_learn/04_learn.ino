// en: Print reusable remote send code
// ja: 再利用できるリモコン送信コードを作る
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

  Serial.println("---- learned ----");
  // en: Use the best candidate when available; otherwise print RAW replay code below.
  // ja: 最上位候補があれば使い、なければ下でRAWの再送コードを表示します。
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
