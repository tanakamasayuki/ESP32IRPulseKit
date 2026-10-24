# ESP32IRPulseKit サンプル

> English: [README.md](README.md)

まず[入門ガイド](../docs/GUIDE_EXAMPLES.ja.md)で配線とAPIの流れを確認してください。対応環境やプロトコルの一覧は[ライブラリREADME](../README.ja.md)を参照します。

| 例 | 学ぶこと |
|---|---|
| [01_rx_dump](01_rx_dump/README.ja.md) | 受信波形とプロトコル候補を調べる |
| [02_nec_tx](02_nec_tx/README.ja.md) | NECコマンドを送信する |
| [03_send_protocols](03_send_protocols/README.ja.md) | プロトコル別の送信ヘルパーを試す |
| [04_learn](04_learn/README.ja.md) | 再利用できるリモコン送信コードを作る |
| [05_raw_monitor](05_raw_monitor/README.ja.md) | RAW受信と取りこぼしを調べる |
| [06_ac_learn](06_ac_learn/README.ja.md) | エアコンの状態信号を受信して学習する |
| [07_ac_send](07_ac_send/README.ja.md) | エアコンの状態を組み立てて送信する |

受信を始めるなら01、送信は02、学習は04へ進みます。受信が不安定なら05、エアコンは06で学習してから07の状態送信を調べます。03は複数プロトコルのAPIを比較する例です。
