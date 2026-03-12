# Clawd Notification Display — TODO

## Status

Firmware builds, flashes, and runs on the Waveshare ESP32-C6-LCD-1.47 board.
BLE advertising works, notifications can be sent and dismissed via BLE GATT writes.
18 C tests pass, 40 Python tests pass (6 test files).
Clawd sprite animations and notification card UI are implemented.
NVS-backed config store supports brightness and sleep timeout with BLE read/write.
macOS menu bar app provides daemon control and device configuration UI.

---

## UI/UX Design (Major)

- [ ] **Notification entry animation** — notification panel fade still stubbed (`/* TODO: fade animation */` in `notification_ui.c`). Scene-width slide animation done (400ms ease-out).
- [ ] **Transition animation** — full-screen new notification → compact list view. Scene width transition done via `lv_anim`. Blocked on notification panel fade (same as entry animation above).
- [ ] **Text truncation and scrolling** — marquee/scroll for overflow text not implemented. Truncation and clipping done (`snprintf` + `LV_LABEL_LONG_CLIP`).

## Python Host Hardening (Medium Priority)

- [ ] **Socket length framing** (`socket_server.py:39`) — `reader.read(4096)` has no message boundary guarantee. Document the 4096-byte limit or switch to newline-framed messages
- [ ] **`sys.exit(1)` in hook** (`clawd-notify:77`) — non-zero exit may surface errors in Claude Code. Consider `sys.exit(0)` since notifications are best-effort
- [ ] **Log file context manager** (`clawd-notify:43`) — `open()` not in `with` block; `Popen` failure leaks the handle
- [ ] **Broad `except Exception`** (`socket_server.py:43`) — use `logger.exception()` for tracebacks and distinguish `JSONDecodeError` from `TimeoutError`

## Testing Improvements (Low Priority)

- [ ] **Add sanitizers to C test Makefile** — `-fsanitize=address,undefined -Werror` catches off-by-one writes in `write_slot`/`memset` at zero cost
- [ ] **Test `_replay_active`** — verify it sends active notifications, handles concurrent mutation
- [ ] **Test BLE write failure → reconnect → replay path**
- [ ] **Test `cwd=""`** (empty string explicitly) — verify `Path("").name` triggers the `"unknown"` fallback

## Code Quality (Low Priority)

- [ ] **Document `_lock_t` locking intent** (`ui_manager.c`) — comment explaining the lock covers both `rebuild_ui()` and `lv_timer_handler()`
- [ ] **Comment `display_init()` return** (`main.c:42`) — return value intentionally discarded; LVGL tracks default display internally
- [ ] **LVGL mutex migration** — consider switching from `_lock_t` to LVGL's built-in `lv_lock()`/`lv_unlock()` for proper flush-ready integration (production hardening)

## Future Considerations (Out of Scope)

- Physical button interaction (dismiss notifications from the device)
- Multiple host device support (pairing with more than one Mac)
- Notification sound/haptic feedback
- OTA firmware updates over WiFi
