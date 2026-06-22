# Compile Tests

> Japanese: [README.ja.md](README.ja.md)

ESP32 build-only checks via Arduino CLI live here.

Scope:

- `examples/` sketches
- Minimal sketches that include the public header
- Compile smoke sketches that use codec, protocol, and frame APIs

These tests only verify compilation. Runtime assertions belong in `tests/pc/codec_smoke/` or `tests/hardware/`.

```sh
uv run --env-file .env pytest pc/compile
```
