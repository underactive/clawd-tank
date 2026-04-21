# Clawd Tank — Agent Map

A physical notification display for Claude Code sessions. Runs on a Waveshare ESP32-C6-LCD-1.47 (320x172 ST7789 SPI display) and shows an animated pixel-art crab ("Clawd") alongside notification cards received over BLE from a Python host daemon.

Three components: **firmware** (ESP-IDF C), **simulator** (native macOS, same C sources), **host** (Python daemon + Claude Code hooks + menu bar app).

## Quick orientation

| What you need                  | Where to look                                                          |
|--------------------------------|------------------------------------------------------------------------|
| System overview & components   | [ARCHITECTURE.md](ARCHITECTURE.md)                                     |
| Build & test commands          | [Build commands](#build-commands) (below)                              |
| Session state model            | [ARCHITECTURE.md](ARCHITECTURE.md#session-state-model)                 |
| Hardware & protocol limits     | [ARCHITECTURE.md](ARCHITECTURE.md#key-constraints)                     |
| BLE protocol history           | [docs/protocol-changelog.md](docs/protocol-changelog.md)               |
| Design principles              | [docs/design-docs/core-beliefs.md](docs/design-docs/core-beliefs.md)   |
| Product specs & user flows     | [docs/product-specs/index.md](docs/product-specs/index.md)             |
| Current execution plans        | [docs/exec-plans/active/](docs/exec-plans/active/)                     |
| Completed plans & history      | [docs/exec-plans/completed/](docs/exec-plans/completed/)               |
| Known tech debt                | [docs/exec-plans/tech-debt-tracker.md](docs/exec-plans/tech-debt-tracker.md) |
| Quality grades by domain       | [docs/QUALITY_SCORE.md](docs/QUALITY_SCORE.md)                         |
| Output & UI conventions        | [docs/DESIGN.md](docs/DESIGN.md)                                       |
| Reliability requirements       | [docs/RELIABILITY.md](docs/RELIABILITY.md)                             |
| Security boundaries            | [docs/SECURITY.md](docs/SECURITY.md)                                   |
| Product sense & taste          | [docs/PRODUCT_SENSE.md](docs/PRODUCT_SENSE.md)                         |
| Plan index                     | [docs/PLANS.md](docs/PLANS.md)                                         |
| Project history & milestones   | [docs/HISTORY.md](docs/HISTORY.md)                                     |
| Reference material             | [docs/references/](docs/references/)                                   |

## Repo conventions

- **Languages:** Firmware and simulator are C (ESP-IDF 5.3.2 for firmware, same sources compiled natively via SDL2 for the simulator). Host is Python 3 — `asyncio` daemon plus a `rumps` menu bar app. The `clawd-tank-notify` hook handler is stdlib-only Python.
- **Boundaries:** BLE JSON payloads are parsed in `ble_service.c` (firmware) and `sim_ble_parse.c` (simulator) before events hit the FreeRTOS/pthread event queue. Claude Code hook stdin is parsed in `clawd-tank-notify` before it forwards to the daemon over a Unix socket. Interior code trusts the parsed structs.
- **Tests:** C unit tests in `firmware/test/` run with `make test`. Host tests are pytest under `host/tests/`. The simulator is the primary integration harness — JSON scenarios in `simulator/scenarios/` exercise end-to-end flows without hardware.
- **Logging:** Firmware uses `ESP_LOG*` macros. Host uses Python stdlib `logging` writing to `~/Library/Logs/ClawdTank/clawd-tank.log`. The menu bar app threads daemon stdout/stderr through the same logger.
- **Naming:** C files are `snake_case.c/h`. Python modules are `snake_case`, classes are `PascalCase`. BLE JSON action names are `snake_case` (`set_sessions`, `set_status`, `set_time`, `add`, `dismiss`, `clear`).
- **File size:** No hard limit; `scene.c` and `ui_manager.c` are the heaviest. Future growth should split out animation subsystems or state machines into separate translation units rather than stretch these further.
- **Imports:** **Never install host Python dependencies (`bleak`, `rumps`, etc.) into the ESP-IDF venv or vice versa.** The firmware and host use independent Python environments. The `clawd-tank-notify` hook handler is stdlib-only by design.
- **Environments:** ESP-IDF is managed by direnv via `firmware/.envrc`. Host venv lives at `host/.venv/`.

## Build commands

### Firmware (ESP-IDF 5.3.2)

ESP-IDF path: `bsp/esp-idf/`. Environment loaded via direnv from `firmware/.envrc`.

```bash
cd firmware && idf.py build                           # Build
cd firmware && idf.py -p /dev/ttyACM0 flash monitor   # Flash + monitor
cd firmware && idf.py fullclean                       # Clean
```

**Do not use the bond-firmware MCP plugin — use `idf.py` directly.**

### Simulator

Requires CMake 3.16+ and SDL2 (`brew install sdl2`) for local dev. Distribution uses static linking.

```bash
cd simulator && cmake -B build && cmake --build build                              # Dynamic SDL2
cd simulator && cmake -B build-static -DSTATIC_SDL2=ON && cmake --build build-static  # Static

./simulator/build/clawd-tank-sim                      # Interactive
./simulator/build/clawd-tank-sim --pinned             # Always on top
./simulator/build/clawd-tank-sim --listen             # TCP listener on port 19872
./simulator/build/clawd-tank-sim --bordered           # Window border (dev)
./simulator/build/clawd-tank-sim --listen --hidden    # Menu bar app mode

# Headless
./simulator/build/clawd-tank-sim --headless \
  --events 'connect; wait 500; notify "clawd-tank" "Waiting for input"; wait 2000; disconnect' \
  --screenshot-dir ./shots/ --screenshot-on-event
./simulator/build/clawd-tank-sim --headless --listen  # Daemon-driven
```

Interactive keys: `c`=connect, `d`=disconnect, `n`=notify, `1-8`=dismiss, `x`=clear, `s`=screenshot, `z`=sleep, `q`=quit.

TCP window commands (JSON over `--listen`):
- `{"action":"show_window"}` / `{"action":"hide_window"}`
- `{"action":"set_window","pinned":true}`
- `{"action":"query_state"}` — returns JSON with slot state, animations, positions

Outbound events (simulator → client): `{"event":"window_hidden"}` when the user closes the window (Cmd+W / X).

### Menu bar app (.app bundle)

```bash
cd host && ./build.sh --install   # Build + install to /Applications
cd host && ./build.sh             # Build only
```

The build script rebuilds the static simulator, runs `py2app`, and copies the sim binary into the `.app` bundle.

### Tests

```bash
cd firmware/test && make test                              # C unit tests (notification + config stores)
cd host && .venv/bin/pytest -v                             # Host Python tests
cd host && .venv/bin/pytest tests/test_protocol.py -v      # Single test

# Bootstrap host venv
cd host && python3 -m venv .venv && .venv/bin/pip install -r requirements-dev.txt
```

### Sprite pipeline

```bash
python tools/svg2frames.py <input.svg> <output_dir/> --fps 8 --duration auto --scale 4
python tools/png2rgb565.py <input_dir> <output.h> --name <sprite_name>
python tools/crop_sprites.py              # Auto-crop in-place
python tools/crop_sprites.py --dry-run    # Preview only
python tools/analyze_sprite_bounds.py
```

### BLE debugging

```bash
python tools/ble_interactive.py           # Connect, send notifications, read/write config
```

## Agent workflow

1. Read this file first for orientation.
2. Check [docs/PLANS.md](docs/PLANS.md) at the start of any session for current project status, active plans, and the release log.
3. Consult the table above to find the right doc for your task domain.
4. For complex work (3+ domains, sequential dependencies, non-obvious decisions, or multi-session scope), check [docs/exec-plans/active/](docs/exec-plans/active/) for an existing plan. If none exists, **create one** using the template in [docs/PLANS.md](docs/PLANS.md) before starting implementation. Update [docs/PLANS.md](docs/PLANS.md) when adding or completing plans.
5. Before committing: build the affected component (`idf.py build`, `cmake --build`, or pytest) and run its tests.
6. If you add a new BLE action or change a JSON shape, update [docs/protocol-changelog.md](docs/protocol-changelog.md). Bump the protocol version GATT characteristic if the change breaks v2 consumers.
7. If you add, rename, or remove a sprite animation, update [ARCHITECTURE.md](ARCHITECTURE.md).
8. If you add or change a user-facing behavior (daemon transport, menu bar control, hook handler), update or create the relevant spec in [docs/product-specs/](docs/product-specs/) using [docs/product-specs/\_template.md](docs/product-specs/_template.md). Add it to [docs/product-specs/index.md](docs/product-specs/index.md).
9. If you ship or significantly change a domain, update its grade in [docs/QUALITY_SCORE.md](docs/QUALITY_SCORE.md).
10. If you discover tech debt, log it in [docs/exec-plans/tech-debt-tracker.md](docs/exec-plans/tech-debt-tracker.md).
11. After shipping a release or a significant change, add an entry to the completed-work log in [docs/PLANS.md](docs/PLANS.md).
12. If you change user-facing interfaces, defaults, or configuration, update [README.md](README.md) to match.

## What NOT to do

- Do not install host Python deps (`bleak`, `rumps`, etc.) into the ESP-IDF venv, or vice versa.
- Do not use the bond-firmware MCP plugin for firmware builds — use `idf.py` directly.
- Do not assume LVGL 8.x APIs. This project pins LVGL 9.5.0 and the API differs significantly.
- Do not send BLE JSON payloads above 256 bytes (MTU limit).
- Do not exceed the ESP32-C6's ~200 KB free-heap budget with sprite frames or dynamic allocations.
- Do not put long instructions in this file — add them to the appropriate doc and link from here.
