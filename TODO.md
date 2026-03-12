# Clawd Notification Display — TODO

## Status

Firmware builds, flashes, and runs on the Waveshare ESP32-C6-LCD-1.47 board.
BLE advertising works, notifications can be sent and dismissed via BLE GATT writes.
All 37 tests pass (11 C + 26 Python). Clawd sprite animations and notification card UI are implemented.

---

## UI/UX Design (Major)

- [x] **Clawd sprite sheet** — 8-bit pixel art crab in RGB565 bitmaps stored in flash
  - Idle animation (breathing/blinking)
  - Alert state (new notification — bouncing, waving claws)
  - Happy state (all notifications cleared)
  - Sleeping/disconnected state
- [ ] **Notification entry animation** — Clawd reacting + notification sliding in from the right
  - Scene-width slide animation done (400ms ease-out). Notification panel fade still stubbed (`/* TODO: fade animation */` in `notification_ui.c`).
- [x] **List view layout** — compact layout for multiple notifications in the right 2/3 of the screen
- [ ] **Transition animation** — full-screen new notification → compact list view
  - Scene width transition done via `lv_anim`. Blocked on notification panel fade (same as entry animation above).
- [x] **Typography and colors** — font choice and color palette for the 320x172 screen
- [x] **Idle/disconnected screen** — what shows when no notifications are active
- [ ] **Text truncation and scrolling** — handle long project names and messages, scrolling for 5+ notifications
  - Truncation and clipping done (`snprintf` + `LV_LABEL_LONG_CLIP`). Marquee/scroll for overflow text not implemented.
- [x] **Notification ordering** — render entries by insertion `seq` order instead of slot index

## Hook Integration (Setup)

- [x] **Install Claude Code hooks** — `host/install-hooks.sh` now auto-merges hooks into `~/.claude/settings.json` via jq
- [ ] **Test full hook→daemon→BLE→display pipeline** — verify notifications appear when Claude Code goes idle and dismiss when user responds

## Firmware Hardening (Medium Priority)

Unchecked return values that can cause silent failures on hardware:

- [x] **`ble_gatts_count_cfg` / `ble_gatts_add_svcs`** — abort on failure (fatal misconfiguration)
- [x] **`ble_hs_mbuf_to_flat`** — check return value, return `BLE_ATT_ERR_UNLIKELY` on failure
- [x] **`lv_display_create`** — NULL check with abort
- [x] **`xTaskCreate`** — check return against `pdPASS`, abort on failure
- [x] **`ble_svc_gap_device_name_set`** — check return value, log warning
- [x] **`ble_hs_util_ensure_addr(0)`** — added before `start_advertising()` in `ble_on_sync`
- [x] **DMA buffer `assert` → `configASSERT`** — idiomatic FreeRTOS pattern

## Python Host Hardening (Medium Priority)

- [x] **`_ble_sender` ValueError crash** — try/except ValueError in `_ble_sender` and `_replay_active`, logs error and continues
- [x] **Failed BLE dismiss drops silently** (`daemon.py:79`) — now triggers reconnect + `_replay_active` on write failure instead of silently dropping
- [ ] **Socket length framing** (`socket_server.py:39`) — `reader.read(4096)` has no message boundary guarantee. Document the 4096-byte limit or switch to newline-framed messages
- [ ] **`sys.exit(1)` in hook** (`clawd-notify:77`) — non-zero exit may surface errors in Claude Code. Consider `sys.exit(0)` since notifications are best-effort
- [ ] **Log file context manager** (`clawd-notify:43`) — `open()` not in `with` block; `Popen` failure leaks the handle
- [ ] **Broad `except Exception`** (`socket_server.py:43`) — use `logger.exception()` for tracebacks and distinguish `JSONDecodeError` from `TimeoutError`

## Testing Improvements (Low Priority)

- [ ] **Add sanitizers to C test Makefile** — `-fsanitize=address,undefined -Werror` catches off-by-one writes in `write_slot`/`memset` at zero cost
- [ ] **Test `_replay_active`** — verify it sends active notifications, handles concurrent mutation
- [ ] **Test BLE write failure → reconnect → replay path**
- [x] **Test unknown event in `_handle_message`** — `test_ble_sender_skips_unknown_event` covers the full sender loop path
- [ ] **Test `cwd=""`** (empty string explicitly) — verify `Path("").name` triggers the `"unknown"` fallback

## Code Quality (Low Priority)

- [ ] **Document `_lock_t` locking intent** (`ui_manager.c`) — comment explaining the lock covers both `rebuild_ui()` and `lv_timer_handler()`
- [ ] **Comment `display_init()` return** (`main.c:42`) — return value intentionally discarded; LVGL tracks default display internally
- [x] **`install-hooks.sh` add `set -u`** — now uses `set -eu`
- [ ] **LVGL mutex migration** — consider switching from `_lock_t` to LVGL's built-in `lv_lock()`/`lv_unlock()` for proper flush-ready integration (production hardening)

## Future Considerations (Out of Scope)

- Physical button interaction (dismiss notifications from the device)
- Multiple host device support (pairing with more than one Mac)
- Notification sound/haptic feedback
- OTA firmware updates over WiFi
