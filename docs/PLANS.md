# Plans Index

Plans are first-class artifacts in this project. Complex, scoped, multi-session work gets an execution plan checked into `docs/exec-plans/`. Small changes use ephemeral plans (inline in the PR description or a task tool).

This file also tracks the plan archive (individual plan docs in `exec-plans/completed/`), the release log — completed work grouped by version — and the list of future considerations currently out of scope.

## Current status (v1.4.0)

Firmware builds, flashes, and runs on the Waveshare ESP32-C6-LCD-1.47 board. BLE advertising works; notifications can be sent and dismissed via BLE GATT writes. 23 C tests pass (with ASan+UBSan), 220 Python tests pass (14 test files). Clawd sprite animations and notification card UI are implemented. NVS-backed config store supports brightness and session timeout with BLE read/write. macOS menu bar app provides daemon control, device configuration UI, and simulator toggle.

Daemon supports multi-transport (BLE + TCP simulator) with dynamic add/remove at runtime. Simulator supports TCP listener (`--listen`) for daemon-driven operation without hardware. Tool-aware working animations — 6 distinct Clawd animations based on Claude Code tool type. 18 animated sprites integrated into `scene.c`, auto-cropped to tight bounding boxes. Multi-session display with up to 4 concurrent Clawd sprites (v2 protocol).

Session state tracking in daemon with priority-based display state computation. Staleness eviction replaces timer-based sleep — sleep is now session-driven. Subagent lifecycle tracking prevents sleeping during long-running agent tasks. Session state persisted to disk — restarting the app preserves display state.

Build script (`host/build.sh`) automates simulator + py2app + bundle. Auto-update hooks, daemon health monitoring, orphan sim cleanup, launchd auto-migration. Firmware uses RGB565A8 pixel format + cropped sprites to fit multi-session in ~200 KB SRAM (no PSRAM). Custom app icon. Proactive BLE reconnection with full state sync on disconnect.

## Active plans

| Plan | Goal | Owner | Started |
|------|------|-------|---------|
| (none) | — | — | — |

See [exec-plans/active/](exec-plans/active/) for full plan documents.

## Completed plans

Individual plan documents archived under [exec-plans/completed/](exec-plans/completed/). Sorted by completion date (ascending). The product-level release log is further below — use that for "what shipped in what version," and use this table for the full plan doc, decisions, and step history.

| Date       | Plan                                                                                                         | Goal                                                                                                                                  |
|------------|--------------------------------------------------------------------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------|
| 2026-03-11 | [clawd-notification-display](exec-plans/completed/2026-03-11-clawd-notification-display.md)                  | Build ESP32-C6 firmware + Python host daemon showing Claude Code "waiting for input" notifications on a 1.47" LCD with Clawd mascot. |
| 2026-03-12 | [clawd-sprite-pipeline](exec-plans/completed/2026-03-12-clawd-sprite-pipeline.md)                            | Build the full sprite design → export → firmware integration pipeline and the pixel-art UI for the 320x172 LCD.                       |
| 2026-03-12 | [hardening-and-hooks](exec-plans/completed/2026-03-12-hardening-and-hooks.md)                                | Fix the daemon's BLE sender `ValueError`, add missing firmware error checks, and auto-merge hooks into Claude Code settings.          |
| 2026-03-12 | [macos-menubar-app](exec-plans/completed/2026-03-12-macos-menubar-app.md)                                    | Add a macOS status bar app wrapping the daemon with device config (brightness, sleep timeout) and connection/notification status.     |
| 2026-03-12 | [rle-sprite-compression](exec-plans/completed/2026-03-12-rle-sprite-compression.md)                          | Compress sprite data from 13.3 MB → ~319 KB via RLE, switch to single-frame decompression, bump flash to 8 MB.                        |
| 2026-03-12 | [simulator](exec-plans/completed/2026-03-12-simulator.md)                                                    | Native macOS simulator that runs the LVGL UI without hardware, in interactive (SDL2) or headless (PNG) mode.                          |
| 2026-03-13 | [menubar-simulator-toggle](exec-plans/completed/2026-03-13-menubar-simulator-toggle.md)                      | Runtime-checkable menu-bar item that enables/disables the simulator TCP transport with persisted preferences.                         |
| 2026-03-13 | [simulator-daemon-bridge](exec-plans/completed/2026-03-13-simulator-daemon-bridge.md)                        | Enable the daemon to drive the simulator over TCP so the full hook → daemon → display pipeline works without hardware.                |
| 2026-03-13 | [working-animations](exec-plans/completed/2026-03-13-working-animations.md)                                  | Add session-aware working animations driven by Claude Code hooks, with intensity tiers based on concurrent session count.             |
| 2026-03-14 | [bundled-simulator](exec-plans/completed/2026-03-14-bundled-simulator.md)                                    | Bundle the static simulator inside the macOS `.app` so users without hardware see the animated Clawd display.                         |
| 2026-03-14 | [daemon-resilience](exec-plans/completed/2026-03-14-daemon-resilience.md)                                    | Detect and recover from orphaned sim processes, silent daemon crashes, outdated hooks, and stale state on startup.                    |
| 2026-03-14 | [session-state-persistence](exec-plans/completed/2026-03-14-session-state-persistence.md)                    | Persist daemon session state to disk so restarting the menu bar app shows the correct animation for already-running sessions.         |
| 2026-03-14 | [subagent-tracking](exec-plans/completed/2026-03-14-subagent-tracking.md)                                    | Prevent Clawd from sleeping during subagent work via `SubagentStart`/`SubagentStop` hook tracking per session.                        |
| 2026-03-14 | [subagent-tracking-future-ideas](exec-plans/completed/2026-03-14-subagent-tracking-future-ideas.md)          | *Backlog, not a completed implementation plan* — ideas that build on subagent tracking (supervising animation, etc.).                 |
| 2026-03-15 | [subagent-multi-session-display](exec-plans/completed/2026-03-15-subagent-multi-session-display.md)          | Display multiple Clawds for multiple sessions and a HUD counter for subagents on the 320×172 display.                                 |
| 2026-03-17 | [stopfailure-hook](exec-plans/completed/2026-03-17-stopfailure-hook.md)                                      | DIZZY animation + error notification card + triple red LED flash when a session hits an API error via `StopFailure`.                  |
| 2026-03-18 | [scene-slot-lifecycle-fixes](exec-plans/completed/2026-03-18-scene-slot-lifecycle-fixes.md)                  | Fix three multi-slot rendering edge cases: narrow mode scenery, disconnect extra Clawds, connect slot adoption.                       |
| 2026-03-19 | [tool-aware-animations](exec-plans/completed/2026-03-19-tool-aware-animations.md)                            | Distinct Clawd animations per Claude Code tool (Read→debugger, Bash→building, Edit→typing, WebSearch→wizard, Agent→conducting, …).    |

## Completed work

Grouped by release, latest first. The BLE protocol-level history lives in [protocol-changelog.md](protocol-changelog.md); this is the product-level release log.

### v1.3.0 — Multi-Session Display & Firmware Memory Optimization

**Multi-Session Display**

- [x] **Multi-session display** — Up to 4 concurrent Clawd sprites with per-session animations. Protocol v2 `set_sessions` action with stable UUIDs. Overflow badge shows "+N" beyond `MAX_VISIBLE=4`.
- [x] **Walk-in animation** — New sessions enter from offscreen with a walking sprite. Existing sessions reposition with walk animations on layout change.
- [x] **Going-away burrowing animation** — Exiting sessions play a burrowing animation. Remaining sessions defer repositioning until burrowing completes.
- [x] **HUD subagent counter** — 2x-scaled mini-crab icon with pixel-art bitmap font shows active subagent count. Overflow badge anchored to right edge.
- [x] **Per-session sweeping** — `PreCompact` sends sweep animation only to the compacting session (v2), global sweep preserved for v1 fallback.
- [x] **Protocol version negotiation** — BLE GATT characteristic exposes protocol version (v2). Daemon reads on connect, selects v1 `set_status` or v2 `set_sessions` per-transport.
- [x] **`query_state` TCP action** — Debug introspection returning JSON with slot states, animations, and positions.
- [x] **Simulator window improvements** — Continuous float scaling, aspect ratio enforcement (328:180), uniform LED border rendering, borderless/resizable window with integer pixel scaling.
- [x] **Custom app icon** — macOS app icon with Clawd pixel-art crab design. SVG source and full iconset in `assets/`.

**Sprite Auto-Crop & Firmware Memory Optimization**

- [x] **Auto-crop sprite pipeline** — `tools/crop_sprites.py` reads existing C headers, decodes all frames, finds tight bounding box, applies symmetric horizontal + free vertical crop, re-encodes RLE, writes headers in-place. `tools/analyze_sprite_bounds.py` for analysis.
- [x] **All 14 sprites cropped** — Frame buffer savings: 1194 KB → 368 KB (69%). Largest session sprite: confused 152x113 (50 KB). Idle: 72x51 (11 KB). Walking: 60x40 (7 KB).
- [x] **RGB565A8 firmware pixel format** — Frame buffers use 3 bytes/pixel (LVGL native-with-alpha for 16-bit display) instead of 4 bytes/pixel ARGB8888. New `rle_decode_rgb565a8` decoder.
- [x] **Firmware slot limits** — `MAX_VISIBLE=4`, `MAX_SLOTS=6` on firmware (no PSRAM). Simulator retains `MAX_SLOTS=8`.
- [x] **`y_offset` adjustments** — All `anim_defs` `y_offset` values recomputed: `new = old - bottom_rows_removed`.
- [x] **Firmware build fixes** — `pixel_font.c` added to CMakeLists, format specifier warnings fixed, unused code suppressed.
- [x] **Heap diagnostics** — Free heap logged at boot. OOM logging in `ensure_frame_buf`.
- [x] **PSRAM correction** — ESP32-C6FH8 has no PSRAM. Removed bogus settings from `sdkconfig.defaults`, corrected internal docs.

### v1.2.1 — Daemon Resilience

- [x] **Auto-update hooks on startup** — Hooks are checked and updated automatically on app launch when outdated, removing the need for manual "Install Hooks" clicks after code updates.
- [x] **Daemon thread crash logging** — Daemon thread exceptions are caught and logged instead of dying silently. Periodic health check timer detects dead daemon and shows disconnected icon.
- [x] **Orphaned sim process cleanup** — On startup, orphaned simulator processes on the listen port are identified by name and killed instead of being connected to.
- [x] **Display state sync on replay** — `_last_display_state` is updated after transport replay to prevent duplicate broadcasts.
- [x] **Proactive BLE reconnection** — Transport sender loop detects dropped connections on each 1s timeout and immediately reconnects with full state sync (time, protocol version, notifications, sessions) instead of waiting for the next hook message.

### v1.2.0 — Subagent Tracking & Session State Persistence

**Subagent Tracking**

- [x] **`SubagentStart`/`SubagentStop` hooks** — New hooks registered and forwarded to daemon via the hook handler script.
- [x] **Per-session subagent tracking** — `subagents: set[agent_id]` tracked per session in daemon state dict.
- [x] **Eviction suppression** — Sessions with active subagents are never evicted by staleness checker.
- [x] **Display state integration** — Sessions with active subagents count as "working" in display state computation, preventing Clawd from sleeping during long subagent tasks.

**Session State Persistence**

- [x] **Atomic session state save/load** — `save_sessions()`/`load_sessions()` in `session_store.py` serialize session state dict to `~/.clawd-tank/sessions.json` with set↔list conversion. Atomic writes via temp file + `os.replace`.
- [x] **Smart persistence** — Session state saved only on structural changes (state transitions, subagent add/remove), not on every `last_event` timestamp update. Reduces disk writes during heavy tool use.
- [x] **Daemon startup recovery** — Loads saved sessions on init with immediate staleness eviction. Restarting the menu bar app immediately shows correct animation for running Claude Code sessions.

### v1.1.0 — Working Animations & Simulator Improvements

**Working Animations**

- [x] **Session state tracking in daemon** — `dict[session_id → state]` with `_compute_display_state()` priority resolution. States: registered → thinking → working → idle → confused. Display priority: working_N > thinking > confused > idle > sleeping.
- [x] **Intensity tiers** — 1 session working = Typing animation, 2 sessions = Juggling, 3+ sessions = Building.
- [x] **Hook protocol expansion** — New hooks: SessionStart, PreToolUse, PreCompact. Hook discriminator field on existing add/dismiss events for daemon-side session state differentiation.
- [x] **6 new sprite animations** — thinking (32f), typing (12f), juggling (10f), building (8f), confused (48f), sweeping (12f, oneshot). All 180×180px @ 8fps. Generated from SVG sources via `svg2frames.py` + `png2rgb565.py` pipeline.
- [x] **Fallback animation mechanism** — `scene_set_fallback_anim()` allows oneshots (alert, happy, sweeping) to return to the current working animation instead of always IDLE.
- [x] **`set_status` BLE/TCP action** — `display_status_t` enum with string-to-enum mapping in both firmware and simulator JSON parsers.
- [x] **Session-driven sleep model** — Timer-based sleep removed from firmware. Sleep is now daemon-driven: no sessions = sleeping. Staleness eviction (configurable timeout, default 10min) handles ungraceful session termination.
- [x] **PreCompact sweeping** — Daemon sends sweeping oneshot followed by computed state as fallback.
- [x] **Hook migration detection** — `are_hooks_installed()` checks all required hooks, not just any. Install button always updates to latest.
- [x] **Menu bar "Session Timeout"** — Renamed from "Sleep Timeout", wired to daemon staleness timeout.

**Simulator Improvements**

- [x] **Default scale changed to 2x** — Window opens at 640×344 instead of 960×516.
- [x] **`--pinned` flag** — Always-on-top mode via `SDL_SetWindowAlwaysOnTop`.
- [x] **Auto-focus on launch** — `SDL_RaiseWindow` brings window to front.
- [x] **Shutdown freeze fix** — Client socket closed during shutdown to unblock `recv()` in listener thread.

### v1.0.0 — Initial Release

**Sprites & Animations**

- [x] **All 5 SVG animations converted to C sprite headers** — idle (96f/135×135), alert (40f/135×135), happy (20f/120×120), sleeping (36f/120×120), disconnected (36f/150×120). Frame buffer cap raised from 48→96. All `#if HAS_*_SPRITE` guards removed from `scene.c`. Alert and happy remain non-looping (one-shot). Idle, sleeping, disconnected loop.

**UI/UX Design**

- [x] **Notification entry animation** — fade animation implemented (300ms ease-out via `lv_anim` + `lv_obj_set_style_opa`). Fade-in on show, fade-out with hide callback on dismiss. Instant path preserved for disconnect/clear.
- [x] **Transition animation** — full-screen new notification → compact list view. New notification triggers a 2.5s hero/expanded card view (fills the notification panel), then animates via `lv_anim` with `lv_anim_path_ease_in_out` over 350ms down to the compact list height. Implemented in `notification_ui.c` via `notification_ui_trigger_hero()` (called from `ui_manager.c` on `BLE_EVT_NOTIF_ADD`). Auto-rotation and repeat-hero on rapid new notifications are both handled correctly.
- [x] **Text truncation and scrolling** — featured card uses `LV_LABEL_LONG_SCROLL_CIRCULAR` marquee for both project name and message. Compact list uses `LV_LABEL_LONG_DOT` ("..."). Manual `snprintf` truncation removed.

**Python Host Hardening**

- [x] **Socket length framing** (`socket_server.py`) — switched to newline-delimited messages; server uses `readline()`, sender appends `\n`.
- [x] **`sys.exit(1)` in hook** (`clawd-notify`) — changed to `sys.exit(0)` with explanatory comment; notifications are best-effort.
- [x] **Log file context manager** (`clawd-notify`) — `open()` now in a `with` block; handle closed even if `Popen` raises.
- [x] **Broad `except Exception`** (`socket_server.py`) — `JSONDecodeError` caught separately with `logger.error`, `TimeoutError` caught explicitly with `logger.warning`, remaining unexpected errors use `logger.exception()` for full traceback.

**Testing Improvements**

- [x] **Add sanitizers to C test Makefile** — `-fsanitize=address,undefined -Werror` added to CFLAGS and LDFLAGS; 18/18 tests pass clean.
- [x] **Test `_replay_active`** — 4 tests: sends all active, empty store, skips unknown events, concurrent mutation safe.
- [x] **Test BLE write failure → reconnect → replay path** — 2 tests: single and multi-notification replay after write failure.
- [x] **Test `cwd=""`** (empty string explicitly) — verified `Path("").name` triggers the `"unknown"` fallback.

**Simulator-Daemon Bridge**

- [x] **TCP socket listener in simulator** — background pthread with mutex-guarded ring buffer queue, newline-delimited JSON protocol matching BLE GATT format. `--listen [port]` CLI flag (default 19872). Shared JSON parser (`sim_ble_parse.c`) mirrors firmware's `parse_notification_json`.
- [x] **Multi-transport daemon architecture** — `TransportClient` Protocol, `SimClient` TCP transport, per-transport queues and sender tasks. `--sim` / `--sim-only` CLI flags. Dynamic `add_transport`/`remove_transport` methods.
- [x] **Per-transport observer status** — `on_connection_change` includes transport name. Menubar shows per-transport status lines (BLE/Simulator: Connected/Connecting...).
- [x] **Simulator toggle in menubar** — "Enable Simulator" checkable menu item with preference persistence (`~/.clawd-tank/preferences.json`). Dynamically adds/removes sim transport at runtime.
- [x] **Initial replay on connect** — `_transport_sender` replays active notifications after initial connect, so dynamically-added transports show existing notifications.
- [x] **Stop hook support** — `protocol.py` handles Stop hook event to show "Waiting for input" notification immediately when Claude stops.

**Code Quality**

- [x] **Document `_lock_t` locking intent** (`ui_manager.c`) — comment explaining the lock covers both `rebuild_ui()` and `lv_timer_handler()`.
- [x] **Comment `display_init()` return** (`main.c:42`) — return value intentionally discarded; LVGL tracks default display internally.
- [x] **LVGL mutex migration** — added TODO comment in `ui_manager.c` noting the `lv_lock()`/`lv_unlock()` migration consideration and flush-ready integration concern.

## Future considerations (out of scope)

- Physical button interaction (dismiss notifications from the device)
- Multiple host device support (pairing with more than one Mac)
- OTA firmware updates over WiFi
- Per-session project name display during working animations

## Creating a plan

Use an execution plan when the work:
- Touches 3+ domains or files
- Requires multiple sequential steps with dependencies
- Involves a non-obvious architectural decision
- Will take more than one session to complete

### Plan template

```markdown
# Plan: <title>

**Goal:** One sentence.
**Status:** Active | Blocked | Completed
**Started:** YYYY-MM-DD

## Context
Why this work is needed.

## Steps
- [ ] Step 1
- [ ] Step 2

## Decisions
- YYYY-MM-DD: Decided X because Y.

## Open questions
- Question?
```

Save to `docs/exec-plans/active/<slug>.md`. Move to `completed/` when done.
