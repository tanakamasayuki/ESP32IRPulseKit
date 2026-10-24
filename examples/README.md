# ESP32IRPulseKit examples

> 日本語版: [README.ja.md](README.ja.md)

Start with the [example guide](../docs/GUIDE_EXAMPLES.md) for wiring and the API workflow. See the [library README](../README.md) for supported environments and protocols.

| Example | Learn |
|---|---|
| [01_rx_dump](01_rx_dump/README.md) | Receive and inspect protocol candidates |
| [02_nec_tx](02_nec_tx/README.md) | Send an NEC command |
| [03_send_protocols](03_send_protocols/README.md) | Compare protocol-specific send helpers |
| [04_learn](04_learn/README.md) | Print reusable remote send code |
| [05_raw_monitor](05_raw_monitor/README.md) | Inspect RAW reception and dropped captures |
| [06_ac_learn](06_ac_learn/README.md) | Capture and learn an air-conditioner state |
| [07_ac_send](07_ac_send/README.md) | Build and transmit an AC state |

Start reception with 01, transmission with 02 and learning with 04. Use 05 to investigate reception, and learn an AC remote with 06 before studying state transmission in 07. Example 03 compares protocol helper APIs.
