# 06_ac_learn — Capture and learn an air-conditioner state

> 日本語版: [README.ja.md](README.ja.md)

Capture a long AC remote burst and print decoded settings and replay code. This receiver-only example does not save data or control an AC by itself.

## Prepare and run

1. Follow the [example guide](../../docs/GUIDE_EXAMPLES.md) for installation and wiring.
2. Connect power, GND and OUT of a demodulating IR receiver; match the input GPIO and inversion setting.
3. Open [06_ac_learn.ino](06_ac_learn.ino), select your board and upload. Open the Serial monitor at 115200 baud.

## Walk through the code

1. Before `rx.begin()`, RAW-only mode, an enlarged symbol capacity and an idle threshold are configured together. The threshold is the silence that ends capture; it must exceed gaps inside the intended multi-frame burst.
2. `ac::decodeAny(r.raw, &Serial)` tries AC decoders and prints a recognized state. Recognizing a vendor does not imply every remote model or optional field is understood.
3. `printSetterSnippet()` creates an editable logical-field template that can lose unrepresented settings. `printSendSnippet()` preserves the decoded state bytes. When unknown, the local helper prints a RAW replay snippet instead.

## Check the result

Press one button once and inspect `raw.len(ticks)` and any `TRUNCATED` marker before copying code. Multiple learned blocks from one press can indicate an idle threshold shorter than an internal gap. Compare several captures before choosing replay data.

## Adapt the example

Keep the default phase-aligned carrier for AC replay. Increase receive capacity before initialization if truncation occurs; tune the idle threshold within the setter/hardware limits rather than assuming any duration is accepted. RAW replay also needs the correct carrier and a complete capture; it is not a guarantee of compatibility. Helpers stay in an anonymous namespace before use so Arduino prototype generation does not need to infer their namespaced types.

`IRReceiveResult::raw` is a view into receiver-owned storage. Copy ticks to your own array if they must survive another `read()` or `end()`. Where this sketch does not print the `begin()` result, check it first when reception never starts.

[Example index](../README.md) · [Library README](../../README.md)
