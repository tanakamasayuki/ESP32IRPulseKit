# PC Tests

> Japanese: [README.ja.md](README.ja.md)

Automated tests that run on a PC with no ESP32 board. This whole group runs in CI.

| Subdirectory | Purpose |
| --- | --- |
| `fixtures/` | Shared IR signal data (`verified/*.yaml`) and Python checks on that data |
| `codec_smoke/` | Runs codec / protocol / Frame logic on an Arduino host build and asserts behavior |
| `compile/` | Compiles examples and minimal sketches for ESP32 (build-only, not run) |

Run the whole group, or one subdirectory:

```sh
uv run --env-file .env pytest pc
uv run pytest pc/fixtures
uv run --env-file .env pytest pc/codec_smoke
uv run --env-file .env pytest pc/compile
```

`fixtures/export_cpp_fixtures.py` generates `codec_smoke/verified_fixtures.h`, which the codec smoke sketch consumes, so the two stay in sync.

Real RMT TX/RX and IR transmission are covered by `tests/hardware/`.
