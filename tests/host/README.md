# Arduino Host Tests

> Japanese: [README.ja.md](README.ja.md)

Runtime logic tests for an Arduino host environment live here.

The goal is to run assertions for logic that does not require real RMT hardware while keeping the test environment close to Arduino users.

Scope:

- `Frame::toBits()` / `fromBits()`
- `encodeBitsToRaw()` / `decodeRawToBits()`
- Protocol spec values
- Tolerance boundaries
- Score and candidate ordering
- Repeat decode
- Decoding fixture RAW into expected BITS

RMT TX/RX, GPIO inversion, idle threshold, and real IR transmission are covered by `tests/hardware/`.

The Arduino host runner is not selected yet. Runtime tests will be added here after that choice is made.
