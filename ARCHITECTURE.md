# Clawd Tank Architecture

## System overview

```
┌─────────────────────────────────────────────────────────────────────────┐
│ Claude Code process                                                     │
│  hooks: SessionStart / PreToolUse / PreCompact / Stop / StopFailure /   │
│         Notification / UserPromptSubmit / SessionEnd /                  │
│         SubagentStart / SubagentStop                                    │
└──────────────┬──────────────────────────────────────────────────────────┘
               │  stdin JSON
               ▼
       ┌───────────────────┐    Unix socket     ┌──────────────────────┐
       │ clawd-tank-notify │ ─────────────────▶ │  clawd_tank_daemon   │
       │  (stdlib-only,    │                    │  (asyncio, multi-    │
       │  installed to     │                    │   transport)         │
       │  ~/.clawd-tank/)  │                    └────────┬─────┬───────┘
       └───────────────────┘                             │     │
                                                BLE      │     │  TCP
                                                ▼              ▼
                                  ┌──────────────────┐   ┌────────────────┐
                                  │  ESP32-C6 device │   │  Simulator     │
                                  │  (firmware, C)   │   │  (same C src,  │
                                  │  NimBLE + LVGL   │   │   SDL2)        │
                                  └──────────────────┘   └────────────────┘
                                        ▲                        ▲
                                        │                        │
                                        └── clawd_tank_menubar ──┘
                                            (rumps status bar app,
                                             spawns daemon + sim)
```

Data flow:

```
Claude Code hooks
    → ~/.clawd-tank/clawd-tank-notify
    → Unix socket
    → clawd_tank_daemon
         dict[session_id → state] → _compute_display_state()
             v2 transports: set_sessions action (per-session anims + UUIDs)
             v1 transports: set_status action (single aggregated animation)
         ~/.clawd-tank/sessions.json (atomic save on structural changes)
    → BLE  → ESP32-C6 firmware
    ↘ TCP → Simulator (SDL2)

Notification cards (daemon):
    hook add/dismiss events
    → _active_notifications
    → add / dismiss actions → device cards
```

## Domain layers

| Layer          | Responsibility                                       | May depend on         |
|----------------|------------------------------------------------------|-----------------------|
| **Assets**     | Sprite headers, fonts, background tiles              | Nothing               |
| **Types**      | BLE JSON schemas, session state enums                | Nothing               |
| **Platform**   | ESP-IDF / simulator shims, display driver, LVGL init | Assets, Types         |
| **Service**    | Notification store, scene engine, BLE GATT server    | Assets, Types, Platform |
| **Coordinator**| `ui_manager` — bridges BLE events to scene + UI      | All above             |
| **Host/UI**    | Python daemon, menu bar app, hook handler            | Types (JSON contract) |

Cross-cutting concerns — logging (`ESP_LOG*` / Python `logging`), time sync, and BLE connection lifecycle — are injected at the coordinator layer. Scene/animation code does not know about BLE; the daemon does not know about LVGL.

The firmware source tree is shared with the simulator via shim headers in `simulator/shims/`. A file that compiles for firmware must compile unchanged for the simulator — ESP-IDF-only APIs stay behind shimmable boundaries.

## Domains

| Domain               | Purpose                                                                       | Status      |
|----------------------|-------------------------------------------------------------------------------|-------------|
| `firmware/main`      | ESP32-C6 firmware entry, event loop, display + BLE init                       | Implemented |
| `firmware/ble`       | NimBLE GATT server, JSON parsing, protocol version characteristic             | Implemented |
| `firmware/scene`     | Clawd sprite animation engine, multi-slot rendering, HUD overlay              | Implemented |
| `firmware/notif`     | Notification ring buffer (max 8) and LVGL card rendering                      | Implemented |
| `firmware/platform`  | SPI bus, ST7789 panel, PWM backlight, WS2812B RGB LED driver                  | Implemented |
| `simulator`          | Native macOS build of firmware sources + SDL2 window + TCP bridge             | Implemented |
| `host/notify`        | Stdlib-only hook handler that forwards Claude Code hook events to the daemon  | Implemented |
| `host/daemon`        | Async multi-transport daemon (BLE + TCP simulator), session state machine     | Implemented |
| `host/menubar`       | macOS rumps status bar app, preferences, simulator process manager            | Implemented |
| `tools/sprite`       | SVG → PNG frames → RLE-compressed RGB565 header pipeline                      | Implemented |

## Firmware (`firmware/main/`)

- **`main.c`** — Entry point. Creates FreeRTOS event queue, inits display/BLE, spawns `ui_task`.
- **`ble_service.c`** — NimBLE GATT server. Parses JSON payloads (`add`/`dismiss`/`clear`/`set_time`/`set_status`/`set_sessions` actions), posts `ble_evt_t` to the event queue. Handles time sync and timezone from the host. Exposes a protocol version GATT characteristic (v2) for capability negotiation.
- **`ui_manager.c`** — State machine coordinator. Bridges BLE events to scene and notification UI. Handles `set_status` for working animations with backlight control for sleep/wake. Drives time display, RGB LED flash, LVGL tick.
- **`scene.c`** — Clawd sprite animation engine. 15 animations (IDLE, ALERT, HAPPY, SLEEPING, DISCONNECTED, THINKING, TYPING, JUGGLING, BUILDING, CONFUSED, DIZZY, SWEEPING, GOING_AWAY, WALKING, MINI_CLAWD). Multi-slot rendering with `MAX_VISIBLE=4` display slots and `MAX_SLOTS=6` on firmware / `MAX_SLOTS=8` on simulator (extra slots for departing animations). Walk-in animation for new sessions, going-away burrowing animation for session exits with deferred repositioning. HUD overlay with pixel-art bitmap font for subagent counter and session overflow badge. Fallback animation mechanism for oneshot return. Manages sky/stars/grass background and scene width transitions (107px with notifications, 320px idle). Firmware uses RGB565A8 pixel format (3 bytes/pixel) for frame buffers; simulator uses ARGB8888 (4 bytes/pixel). Debug introspection via `scene_get_state_json()` for the `query_state` TCP action.
- **`notification_ui.c`** — LVGL card rendering. Auto-rotating featured card + compact list. 8-color accent palette.
- **`notification.c`** — Ring buffer store (max 8 notifications). Tracks by 48-char ID + sequence counter.
- **`rgb_led.c`** — WS2812B driver for the onboard RGB LED (GPIO8). Non-blocking flash with linear fade-out via `esp_timer`.
- **`display.c`** — SPI bus + ST7789 + LVGL + PWM backlight init.
- **`assets/`** — RLE-compressed RGB565 sprite headers generated by `tools/png2rgb565.py`. Sprites are auto-cropped to tight bounding boxes with symmetric horizontal padding (keeps Clawd centered).
- **`pixel_font.c/h`** — Pixel-art bitmap font renderer for HUD overlays (subagent counter, overflow badge).

## Simulator (`simulator/`)

Compiles the **same firmware source files** unmodified. ESP-IDF APIs are replaced by shim headers in `simulator/shims/`. Uses SDL2 for display and `stb_image_write` for PNG capture. Supports inline event strings, JSON scenario files (`scenarios/`), a TCP listener (`--listen [port]`), always-on-top mode (`--pinned`), and window show/hide via TCP commands.

The simulator binary ships inside the Menu Bar `.app` bundle for hardware-free use. It can also be built standalone with `STATIC_SDL2=ON` for a self-contained binary (no Homebrew SDL2 needed).

Key simulator-specific files:

- **`sim_ble_parse.c/h`** — Shared JSON parser for the TCP bridge (mirrors firmware's `parse_notification_json`). Returns `0` (BLE event), `1` (set_time), `2` (config), `3` (window command), or `-1` (error).
- **`sim_socket.c/h`** — TCP listener with mutex-guarded ring buffers for BLE events and window commands (background pthread, main thread drains). Supports outbound events via `sim_socket_send_event()`. Handles the `query_state` action, returning JSON with slot state, animations, and positions.

## Host (`host/`)

- **`clawd-tank-notify`** — Standalone hook handler (installed to `~/.clawd-tank/clawd-tank-notify` by the menu bar app). Reads Claude Code hook stdin, converts to a daemon message, forwards via Unix socket. Uses only stdlib — no external imports.
- **`clawd_tank_daemon/`** — Async Python daemon (asyncio). Multi-transport architecture with a `TransportClient` Protocol. Supports BLE (`ClawdBleClient`) and TCP simulator (`SimClient`) transports with independent per-transport queues and sender tasks. Dynamic transport add/remove at runtime. Per-transport protocol versioning: v1 `set_status` for legacy; v2 `set_sessions` with per-session animations and UUIDs. The BLE client reads the protocol version GATT characteristic on connect. Session state tracking with priority-based display state computation, staleness eviction, subagent lifecycle tracking, and per-session sweeping for v2 transports. `SimProcessManager` manages the simulator subprocess lifecycle (spawn, window commands, stdout/stderr logging through the Python logger, `SIGKILL` on quit). `session_store.py` handles atomic save/load of session state to `~/.clawd-tank/sessions.json` for restart recovery.
- **`clawd_tank_menubar/`** — macOS status bar app (`rumps`). Transport submenus (BLE/Simulator) with independent enable/disable, connection status with colored emoji indicators, simulator window controls (show/hide, always-on-top), brightness/session timeout config, Claude Code hook installer (`hooks.py`), version display, log file output (`~/Library/Logs/ClawdTank/clawd-tank.log`). Preferences persist to `~/.clawd-tank/preferences.json` with a read-modify-write pattern. Auto-updates hooks on startup when the installed version is outdated (no manual "Install Hooks" click required). A periodic health check timer detects daemon thread crashes and shows a disconnected icon; daemon thread exceptions are caught and logged instead of dying silently.

## Session state model

The daemon tracks per-session state and computes display state sent to the device. Protocol v2 transports receive per-session animations; v1 transports receive a single aggregated animation.

- **Per-session states:** `registered` → `thinking` → `working` → `idle` → `confused` / `error`.
- **Protocol v2 (multi-session):** Each session gets its own animation and a stable UUID. Up to `MAX_VISIBLE=4` sessions shown simultaneously with individual Clawd sprites. Per-session sweeping sends the sweep animation only to the compacting session.
- **Protocol v1 (legacy):** Display states (priority order): `working_N` (1-3 sessions) > `thinking` > `confused` > `idle` > `sleeping`. Intensity tiers: 1 session = Typing, 2 = Juggling, 3+ = Building.
- **Session transitions:** New sessions walk in from offscreen. Exiting sessions play a burrowing animation, then remaining sessions reposition with walk animations. Extra sessions beyond `MAX_VISIBLE` are shown as a "+N" overflow badge.
- **Special events:** `PreCompact` → per-session sweeping (v2) or global oneshot sweep (v1); `Notification` (`idle_prompt`) → confused; `StopFailure` (API error) → error/dizzy with triple red LED flash.
- **Staleness eviction:** Sessions with no events within the configurable timeout (default 10 min) are evicted. No sessions = sleeping.
- **Subagent tracking:** `SubagentStart` / `SubagentStop` hooks track active `agent_id`s per session. Sessions with active subagents are never evicted and count as "working" in display state. HUD overlay shows a mini-crab icon with subagent count.
- **Session persistence:** Session state is saved atomically to `~/.clawd-tank/sessions.json` on structural state changes. The daemon loads saved state on startup with immediate stale-session eviction, so restarting the menu bar app preserves the correct display state for running Claude Code sessions.

## Supported hardware

Two boards are currently supported, selected at build time via the Kconfig `CLAWD_BOARD` choice and the matching `idf.py set-target`. Per-board pins, geometry, and capability flags live in `firmware/main/board_config.h`.

| Board | MCU | Display | Flash | PSRAM | Touch | Battery |
|---|---|---|---|---|---|---|
| Waveshare ESP32-C6-LCD-1.47 | ESP32-C6FH8 (RISC-V, single-core) | ST7789, 320×172 SPI, 12 MHz | 8 MB | — | — (BOOT button dismisses) | — |
| Freenove ESP32-S3 2.8" (fnk0104) | ESP32-S3 (dual-core) | ILI9341, 320×240 SPI, 40 MHz | 16 MB QIO-OPI | 8 MB OPI | FT6336G capacitive | Yes (ADC on GPIO 9) |

## Key constraints

- **Display:** Up to 320×240 pixels, 16-bit RGB565, SPI. All UI must fit the shortest supported display (172 rows on the C6 board). Layout constants come from `board_config.h`.
- **BLE MTU:** 256 bytes. Notification JSON payloads must stay under this limit.
- **Notification limit:** 8 simultaneous (ring buffer, oldest dropped on overflow).
- **LVGL version:** 9.5.0 (not 8.x — API differs significantly).
- **Sprite format:** RLE-compressed RGB565 arrays with transparency key color `0x18C5`. Auto-cropped with symmetric horizontal padding. Firmware decodes to RGB565A8 (3 bytes/pixel); simulator decodes to ARGB8888 (4 bytes/pixel). One frame buffer per active slot, lazy-allocated — placed in PSRAM on boards that have it (fnk0104), internal SRAM otherwise.
- **Max sessions on-screen:** `MAX_VISIBLE=4` (protocol limit, both boards). Slot array grows to 8 on PSRAM-equipped boards so walk-in/burrow animations overlap without blocking.
- **RGB LED:** Onboard WS2812B, driven via the `espressif/led_strip` component. Flashes on notifications. Pin differs per board (C6: GPIO 8, fnk0104: GPIO 42).
- **Time sync:** No WiFi/NTP. The host daemon sends epoch + POSIX timezone string over BLE on each connect (`set_time` action).

## Key design decisions

1. **Single C codebase for device and simulator.** The simulator compiles the same firmware sources unmodified. ESP-IDF APIs are hidden behind shim headers in `simulator/shims/`. This keeps bugs reproducible off-hardware and lets hooks/daemon development proceed without an ESP32 in hand.
2. **BLE GATT, not WiFi.** No network stack on the device. Time sync, timezone, and notifications all arrive over a small set of GATT characteristics. Keeps the device deterministic and the firmware small, at the cost of requiring a host daemon nearby.
3. **Multi-transport daemon with per-transport protocol versioning.** The daemon speaks to an ESP32 and a simulator concurrently. Protocol v2 (`set_sessions`) is preferred; v1 (`set_status`) remains for older/legacy consumers. The protocol version is read from a GATT characteristic on BLE connect so each transport is served the correct shape.
4. **Session state is the daemon's job, not the device's.** The device renders whatever animations it is told to render. All priority arbitration, staleness eviction, subagent tracking, and session-slot positioning happen in the daemon. Session state is persisted atomically to `~/.clawd-tank/sessions.json` so the device stays in sync across menu-bar-app restarts.
5. **Notifications are a ring buffer, not a queue.** On overflow the oldest notification is dropped — a user will never see a 30-notification backlog, they will see the 8 most recent.
6. **Sprites are RLE-compressed RGB565 with a magenta-adjacent key color.** `0x18C5` is unlikely in pixel art and keeps decoded frame buffers small enough to live in SRAM. Frame buffers are lazy-allocated per active slot.
7. **RGB565A8 on device, ARGB8888 in simulator.** The device's LVGL build uses RGB565A8 (3 bytes/pixel) for frame buffers. The simulator uses ARGB8888 (4 bytes/pixel) for SDL compatibility. The decode path branches on `#ifdef`.
8. **LVGL 9.5.0 pinned.** The 9.x API differs significantly from 8.x; any third-party examples must be translated before they compile.
9. **Hook handler is stdlib-only.** `clawd-tank-notify` is installed into `~/.clawd-tank/` by the menu bar app and must run under whatever Python is on the user's `PATH`. No `bleak`, no `rumps`, no external dependencies.
10. **Menu bar app owns the simulator subprocess lifecycle.** Spawning, window show/hide, and shutdown (`SIGKILL` on quit) are orchestrated from the menu bar app via `SimProcessManager`. Stdout/stderr are threaded through the Python logger.
