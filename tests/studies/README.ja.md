# Studies（実機調査）

> English: [README.md](README.md)

合否ではなく観測ログを取るオンデマンドの実機調査です。物理的な挙動（キャリア位相ジッター、timing sweep、リンク品質）を特性評価し、外部ライブラリと比較します。結論はログやプロットを人が読んで判断します。

これらは**自動収集されません**。pytestファイルは `test_*.py` ではなく `study_*.py` 命名なので、`pytest .` や `pytest hardware` では拾われません。必要時にパターンを明示して実行します。

```sh
uv run --env-file .env pytest -s -o python_files="study_*.py" studies/carrier_jitter/
```

`analyze.py` / `monitor.py` は通常のスクリプトとして直接実行します。

```sh
uv run python studies/jvc_timing_sweep/analyze.py studies/jvc_timing_sweep/data/jvc_sweep.csv
```

| ディレクトリ | 調査内容 |
| --- | --- |
| `carrier_jitter/` | mark幅・キャリア周波数・dutyに対する復調markエッジの安定性 |
| `carrier_loopback/` | TSOPなし・1µs捕捉でのキャリア位相挙動 |
| `jvc_timing_sweep/` | 外部デコーダに対するJVCゼロ空白窓 |
| `jvc_verify_arduino/` | Arduino-IRremoteに対するJVC decode確認 |
| `tx_jitter/` | PulseKitと外部ライブラリのTX包絡線ジッター（無線） |
| `tx_jitter_loopback/` | 同上、1台有線loopback治具で |
| `link_quality/` | リンク品質のライブメーター |
| `dump/` | `.env`駆動の手動IRダンプ（汎用+AC）、GPIO直書き不要 |
| `compat_matrix/` | protocol差分、bit order、外部ライブラリ互換 |
| `compat_matrix_ac/` | エアコン状態フィールドの外部ACライブラリ互換（Panasonicフィールドマップの較正） |

再現性のある知見は `pc/fixtures` と `hardware/` へ昇格します。
