# Build Tests

> Japanese: [README.ja.md](README.ja.md)

Arduino CLI build tests for ESP32 live here.

Scope:

- `examples/` sketches
- Minimal sketches that include the public header
- Compile smoke sketches that use codec, protocol, and frame APIs

These tests only verify compilation. Runtime assertions belong in `tests/host/` or `tests/hardware/`.

GitHub Actions is expected to run `host` and `build`.
