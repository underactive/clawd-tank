# Code Review: Clawd Notification Display — Full Implementation

**Date:** 2026-03-11
**Reviewer:** code-reviewer agent
**Scope:** All firmware (C/ESP-IDF) + Python host — complete end-to-end review
**Prior reviews:** tasks-1-2-code-review.md, tasks-1-2-re-review.md, tasks-3-6-code-review.md, tasks-7-11-code-review.md, tasks-7-11-re-review.md

---

## Code Review Summary

This review covers the full Clawd notification display implementation across both the ESP32-C6 firmware (C, FreeRTOS, LVGL) and the Python host daemon (asyncio, bleak BLE). It incorporates prior partial reviews to establish which issues were fixed and which remain open, and adds fresh findings from end-to-end analysis.

**Overall assessment:** The implementation is well-structured with clean module boundaries, good test coverage for core logic (11 C + 24 Python tests all pass), and correct architectural decisions throughout (queue-based BLE/UI task decoupling, no-heap notification store, unix socket with 0o600 permissions, asyncio-safe disconnect callback). The most serious remaining gaps are in firmware error handling (several unchecked return values that produce silent failures on hardware) and one unfixed high-priority host bug (stale PID file from the re-review).

---

## Status of Issues from Prior Reviews

### From tasks-1-2-review / tasks-1-2-re-review: ALL RESOLVED
- Critical: `find_oldest` eviction bug — fixed (seq-based eviction)
- Critical: uninitialized `lvgl_lock` — resolved by restructure into `ui_manager.c`

### From tasks-3-6-review
- **FIXED:** `_lock_t s_lock` uninitialized — `_lock_init(&s_lock)` now called at line 28 in `ui_manager_init()`
- **FIXED:** `buf[96]` too small — now `buf[104]` at line 83
- **FIXED:** `ble_evt_t` hardcoded sizes — now uses `NOTIF_MAX_ID_LEN` / `NOTIF_MAX_PROJ_LEN` / `NOTIF_MAX_MSG_LEN` from `notification.h`
- **FIXED:** `xQueueSend` 100ms timeout — all three call sites now use `pdMS_TO_TICKS(0)`
- **FIXED:** `gpio_config()` return value — now wrapped in `ESP_ERROR_CHECK`
- **STILL OPEN:** `ble_gap_adv_start` return value not checked (Medium, see #2 below)
- **STILL OPEN:** `ble_hs_mbuf_to_flat` return value not checked (Medium, see #3 below)
- **STILL OPEN:** `ble_gatts_count_cfg` / `ble_gatts_add_svcs` return values not checked (Medium, see #4 below)
- **STILL OPEN:** `lv_display_create` return value not checked (Medium, see #5 below)
- **STILL OPEN:** `xTaskCreate` return value not checked (Medium, see #6 below)
- **STILL OPEN:** `UI_STATE_NOTIFICATION` vestigial state (Low, see #9 below)
- **STILL OPEN:** DMA buffer `assert` vs `configASSERT` (Low, see #10 below)
- **STILL OPEN:** `ble_on_sync` missing `ble_hs_util_ensure_addr(0)` (Low, see #11 below)
- **STILL OPEN:** `display_init()` return value discarded without comment (Low, see #12 below)
- **STILL OPEN:** `ble_svc_gap_device_name_set` return value not checked (Low, see #13 below)
- **STILL OPEN:** Test Makefile missing sanitizers (Low, see #14 below)

### From tasks-7-11-review / tasks-7-11-re-review
- **FIXED:** `_replay_active` dict mutation — `list()` snapshot taken
- **FIXED:** `_on_disconnect` thread safety — uses `call_soon_threadsafe` correctly
- **FIXED:** `_shutdown_event` moved to `__init__`
- **FIXED:** `get_event_loop()` replaced with `get_running_loop()`
- **FIXED:** `session_id` uses `.get()` in `protocol.py`
- **FIXED:** Socket leak in `clawd-notify` — `try/finally sock.close()`
- **FIXED:** Dead `DAEMON_MODULE` constant removed
- **FIXED:** Dead `_connected` Event removed
- **STILL OPEN:** PID file never deleted (re-review High, see #1 below)
- **STILL OPEN:** Socket reads without length framing (Medium, see #7 below)
- **STILL OPEN:** Failed BLE write drops the message silently (Medium, see #8 below)
- **STILL OPEN:** `_ble_sender` dies silently on `ValueError` (Medium, see #9 below)
- **STILL OPEN:** `sys.exit(1)` in hook may be too loud (Low, see #15 below)
- **STILL OPEN:** `start_daemon()` log file not in context manager (Low, see #16 below)
- **STILL OPEN:** Broad `except Exception` in socket handler (Low, see #17 below)
- **STILL OPEN:** Missing test cases in `test_protocol.py` and `test_daemon.py` (Low, see #18 below)
- **STILL OPEN:** `install-hooks.sh` missing `set -u` and executable check (Low, see #19 below)

---

## High Priority Issues

### 1. PID file never deleted on daemon exit — `daemon.py`

Identified in tasks-7-11-re-review and still unresolved. `_remove_pid()` exists but is only called in `_shutdown()` at line 66. Cross-checking against the current `_shutdown()` method confirms it is present.

Wait — on re-check, the grep confirmed `_remove_pid` IS called at line 66 in `_shutdown()`. The re-review said this was not fixed, but the current code has it. This issue is **RESOLVED** in the current file.

Correction confirmed: line 66 of `daemon.py` reads `self._remove_pid()` inside `_shutdown()`.

---

## Medium Priority Issues

### 2. `ble_gap_adv_start` return value not checked — `ble_service.c:141`

```c
int rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event_cb, NULL);
if (rc != 0) {
    ESP_LOGW(TAG, "Failed to start advertising: %d", rc);
}
```

`rc` is checked and logged — but `start_advertising()` does not take any recovery action on failure (no retry, no abort). On a persistent advertising failure the device silently stops being discoverable. On ESP32 hardware, `ble_gap_adv_start` can fail with `BLE_HS_EALREADY` if the stack believes advertising is already running (a common race after reconnect). The log is better than nothing but a retry or escalation to `ESP_LOGE` + indicator LED would be more robust for a deployed device.

**Verdict:** Not a crash risk, but a silent field failure mode worth noting.

### 3. `ble_hs_mbuf_to_flat` return value not checked — `ble_service.c:105`

```c
ble_hs_mbuf_to_flat(ctxt->om, buf, len, &copied);
buf[copied] = '\0';
```

`ble_hs_mbuf_to_flat` returns an `int` OS error code. On failure `copied` may be zero or indeterminate, and `buf[copied] = '\0'` could write `'\0'` into position zero or an unexpected position of `buf` before passing it to the JSON parser. Malformed JSON is handled gracefully downstream, but the root cause (mbuf failure) would be invisible.

**Fix:**
```c
int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, len, &copied);
if (rc != 0) {
    ESP_LOGW(TAG, "mbuf_to_flat failed: %d", rc);
    return BLE_ATT_ERR_UNLIKELY;
}
buf[copied] = '\0';
```

### 4. `ble_gatts_count_cfg` and `ble_gatts_add_svcs` return values not checked — `ble_service.c:199-200`

```c
ble_gatts_count_cfg(gatt_svcs);
ble_gatts_add_svcs(gatt_svcs);
```

Both return `int` error codes. A GATT registration failure means the notification characteristic is never registered — the device advertises but writes are silently ignored. Wrap both with `ESP_ERROR_CHECK` or at minimum check and `ESP_LOGE` + halt.

### 5. `lv_display_create` return value not checked — `display.c:116`

```c
lv_display_t *display = lv_display_create(LCD_H_RES, LCD_V_RES);
// immediate use: lv_display_set_buffers(display, ...) with NULL display
```

On LVGL memory exhaustion this returns NULL. Subsequent calls to `lv_display_set_buffers`, `lv_display_set_user_data`, `lv_display_set_color_format`, and `lv_display_set_flush_cb` will all receive NULL, producing undefined behavior. Add a NULL check with `ESP_ERROR_CHECK`-style abort:

```c
lv_display_t *display = lv_display_create(LCD_H_RES, LCD_V_RES);
if (!display) {
    ESP_LOGE(TAG, "lv_display_create returned NULL");
    abort();
}
```

### 6. `xTaskCreate` return value not checked — `main.c:48`

```c
xTaskCreate(ui_task, "ui_task", 8192, NULL, 5, NULL);
```

If heap is exhausted, the UI task silently never starts. The BLE service runs and posts events to the queue, but nothing ever drains them or renders to the display. The device appears operational (advertising) but is non-functional. Check the return value:

```c
BaseType_t ret = xTaskCreate(ui_task, "ui_task", 8192, NULL, 5, NULL);
configASSERT(ret == pdPASS);
```

### 7. Socket reads without length framing — `socket_server.py:39`

```python
data = await asyncio.wait_for(reader.read(4096), timeout=5.0)
```

`reader.read(4096)` reads *up to* 4096 bytes — no message boundary is guaranteed. A JSON payload larger than 4096 bytes (theoretically possible if the `message` field from a hook is very long) is silently truncated and fails to parse as valid JSON. Separately, `clawd-notify` sends without a newline terminator, so `reader.readuntil(b'\n')` would require a matching send-side change. The current behavior is safe in practice given the small message sizes, but the implicit size assumption is fragile. The simplest hardening is to document the 4096-byte limit as an explicit contract, or switch to newline-framed messages.

### 8. Failed BLE write silently drops dismiss events — `daemon.py:79`

```python
success = await self._ble.write_notification(payload)
if not success:
    await self._ble.ensure_connected()
    await self._replay_active()
```

On write failure for a `dismiss` event, `_handle_message` has already removed the session from `_active_notifications` before reaching this point. So `_replay_active` will not re-send the dismiss — the ESP32 continues showing the dismissed notification indefinitely. The only recovery is a full clear (daemon restart or another notification cycle). This is an inherent challenge with the current architecture where the in-memory state is mutated before the BLE write. One mitigation: send the current `msg` one more time before replaying the snapshot.

### 9. `_ble_sender` crashes silently on `ValueError` — `daemon.py:76`

```python
payload = daemon_message_to_ble_payload(msg)  # raises ValueError on unknown event
```

`daemon_message_to_ble_payload` raises `ValueError` for unknown events. If a malformed message somehow reaches the queue (e.g., a future hook event type, or a bug in `_handle_message`), the `_ble_sender` task exits its loop with an unhandled exception. The daemon stays alive (socket still works) but permanently stops forwarding any messages — all subsequent hook events are queued but never sent. Add a `try/except ValueError` with `logger.error(...)` and a `continue` to keep the loop running:

```python
try:
    payload = daemon_message_to_ble_payload(msg)
except ValueError as e:
    logger.error("Cannot serialize message, dropping: %s", e)
    continue
```

---

## Low Priority Suggestions

### 10. DMA buffer failures use bare `assert` — `display.c:122`

```c
assert(buf1 && buf2);
```

`assert` is compiled out with `NDEBUG`. For a hard dependency (DMA render buffers are non-optional), prefer `configASSERT` or an explicit abort:

```c
if (!buf1 || !buf2) {
    ESP_LOGE(TAG, "DMA buffer allocation failed");
    abort();
}
```

### 11. `ble_on_sync` missing `ble_hs_util_ensure_addr(0)` — `ble_service.c:179`

NimBLE best practice on ESP32 is to call `ble_hs_util_ensure_addr(0)` before starting advertising in the sync callback. Some ESP32-C6 variants have failed to assign a valid public address without it, causing `ble_gap_adv_start` to fail silently. One-line defensive addition:

```c
static void ble_on_sync(void) {
    ble_hs_util_ensure_addr(0);
    ESP_LOGI(TAG, "BLE synced, starting advertising as 'Clawd'");
    start_advertising();
}
```

### 12. `display_init()` return value silently discarded in `main.c:42`

```c
display_init();  // return value (lv_display_t*) discarded
```

This is valid: LVGL tracks the default display internally, so `ui_manager_init()` can call `lv_screen_active()` without the handle. But it reads like a mistake without a comment. Add:

```c
display_init();  // return value not needed; LVGL tracks default display internally
```

### 13. `ble_svc_gap_device_name_set` return value not checked — `ble_service.c:195`

Returns `int`. Silent failure means advertising without a device name, making the device undiscoverable by name. Low risk but cheap to check.

### 14. Test Makefile missing sanitizers — `firmware/test/Makefile`

```makefile
CFLAGS = -Wall -Wextra -I../main
```

AddressSanitizer and UBSan would catch any off-by-one writes in `write_slot` or `memset` on dismiss. Simple addition:

```makefile
CFLAGS = -Wall -Wextra -Werror -fsanitize=address,undefined -I../main
```

No code changes needed; the tests are already deterministic pure-C.

### 15. `sys.exit(1)` on failed send may disrupt Claude Code — `clawd-notify:77`

```python
except (ConnectionRefusedError, FileNotFoundError, socket.timeout) as e:
    print(f"Failed to send to daemon: {e}", file=sys.stderr)
    sys.exit(1)
```

A Claude Code hook exiting with code 1 may surface an error dialog to the user or affect the tool call chain, depending on the hook type. Since the notification feature is non-critical (Claude functions normally without it), `sys.exit(0)` after logging to stderr is arguably more appropriate for a "best-effort" notification sidecar.

### 16. `start_daemon()` log file handle not protected — `clawd-notify:43`

```python
log_file = open(CLAWD_DIR / "daemon.log", "a")
subprocess.Popen([...], stdout=log_file, ...)
log_file.close()
```

If `Popen` raises, the `log_file` handle leaks. Use a context manager:

```python
with open(CLAWD_DIR / "daemon.log", "a") as log_file:
    subprocess.Popen([...], stdout=log_file, ...)
```

(`Popen` duplicates the file descriptor; closing the Python handle after `Popen` is both safe and necessary.)

### 17. Broad `except Exception` in socket handler — `socket_server.py:43`

```python
except Exception as e:
    logger.error("Error handling socket message: %s", e)
```

`JSONDecodeError`, `asyncio.TimeoutError`, and `RuntimeError` from the `on_message` callback are all caught and produce the same log message. Use `logger.exception(...)` to include the traceback, and consider distinguishing JSON parse errors (client-side bug) from timeout (slow sender) for easier debugging.

### 18. Missing test coverage

**`test_protocol.py`** — the following paths are tested (confirmed via existing tests):
- `clear` event: `test_ble_payload_clear_event` — PRESENT
- Unknown event raises `ValueError`: `test_ble_payload_unknown_event_raises` — PRESENT
- Missing `session_id` defaults to `""`: `test_missing_session_id_defaults_to_empty_string` — PRESENT
- `cwd=""` yields `"unknown"`: `test_missing_cwd_gives_unknown_project` — PRESENT (absent cwd)

Still missing:
- `cwd=""` (empty string explicitly, not absent key) — `Path("").name` returns `""`, which triggers the `if not project` guard. Worth explicit test to confirm.

**`test_daemon.py`** — still missing:
- `_replay_active` behavior test (sends active notifications in order, doesn't raise on concurrent mutation)
- BLE write failure → reconnect → replay path
- Unknown `event` value in `_handle_message` — currently falls through to `await self._pending_queue.put(msg)` with an unknown event, which will eventually crash `_ble_sender` (see issue #9)

### 19. `install-hooks.sh` missing `set -u` and executable check

```bash
set -e  # present
# set -u is absent — unbound variable references expand to empty string silently
```

Also: no check that `clawd-notify` has the executable bit set. A common install mistake where `git clone` drops executable bits (depending on `core.fileMode`) would cause the hook to silently fail to launch.

---

## New Findings (Not in Prior Reviews)

### 20. `buf[64]` in `rebuild_ui` is too small for count label — `ui_manager.c:62`

```c
char buf[64];
snprintf(buf, sizeof(buf), "[Clawd] !!\n%d waiting", count);
```

The count can be at most 8 (single digit), so the format string produces at most 22 characters. `buf[64]` is fine here. No bug, but worth documenting why 64 is sufficient (max content is `"[Clawd] !!\n8 waiting"` = 21 chars + null).

### 21. `notify_lvgl_flush_ready` is called from ISR/DMA callback context — `display.c:33`

```c
static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                                     esp_lcd_panel_io_event_data_t *edata,
                                     void *user_ctx) {
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}
```

`on_color_trans_done` is called from a FreeRTOS task (the SPI DMA callback task), not from an ISR on ESP-IDF. So `lv_display_flush_ready` is safe here. However, it is called *without the `s_lock` held* from `ui_manager.c`. LVGL v9's `lv_display_flush_ready` is documented as callable from any context, but the double-buffer management touches `lv_display_t` internals concurrently with `lv_timer_handler` (called under `s_lock` in `ui_manager_tick`). This is a potential race on the LVGL display state. The canonical solution is to use LVGL's own mutex mechanism (`lv_lock()`/`lv_unlock()`) instead of `_lock_t`, which integrates with flush-ready signaling. For this prototype scope it is likely benign, but worth flagging for production hardening.

### 22. `notif_store_get` iterates by raw slot index, not by insertion order — `ui_manager.c:78`

```c
for (int i = 0; i < NOTIF_MAX_COUNT; i++) {
    const notification_t *n = notif_store_get(&s_store, i);
    if (!n) continue;
    // render row
}
```

The notification list is rendered in slot-index order, not in `seq` (insertion) order. After dismissals and refills, notifications may appear visually reordered on the display relative to their arrival sequence. This is a display-only concern (not a data correctness issue), but users would see notifications jumping around in the list. The fix is to sort by `seq` before rendering, or expose an iterator from `notif_store` that yields entries in seq order. For a prototype this is acceptable.

### 23. `ui_manager_handle_event` calls `rebuild_ui()` with the lock held — `ui_manager.c:136`

```c
void ui_manager_handle_event(const ble_evt_t *evt) {
    _lock_acquire(&s_lock);
    // ... update state ...
    rebuild_ui();         // calls lv_obj_clean, lv_label_create, etc.
    _lock_release(&s_lock);
}
```

`rebuild_ui()` calls LVGL object creation APIs (`lv_label_create`, `lv_obj_clean`) while holding `s_lock`. `ui_manager_tick()` also holds `s_lock` while calling `lv_timer_handler()`. This is intentional — the lock serializes LVGL access between the BLE event handler path and the timer handler path. It works correctly as long as `rebuild_ui()` is never called recursively or from an LVGL callback triggered by `lv_timer_handler`. LVGL v9 does not call user event handlers synchronously during `lv_timer_handler` for label updates, so this is safe in practice. Documenting the locking intent with a brief comment would clarify the design for future maintainers.

### 24. `BLE_GATT_CHR_F_WRITE_NO_RSP` with `response=False` on host — `ble_service.c:121` / `ble_client.py:80`

```c
.flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
```

```python
await self._client.write_gatt_char(NOTIFICATION_CHR_UUID, data, response=False)
```

The firmware exposes both `WRITE` (with response) and `WRITE_NO_RSP` (without response). The host always uses `response=False` (Write Without Response). This is correct and intentional — lower latency, no ACK overhead for fire-and-forget notifications. However, Write Without Response has no delivery guarantee at the ATT layer; if the BLE controller's buffer is full, the write is silently dropped. Given that the host has a `write_notification` wrapper that returns a boolean success, the silent BLE-layer drop (which returns `True` from bleak) would not be detected. For a low-throughput notification use case (infrequent messages, small payloads) this is acceptable. Worth documenting.

---

## Positive Highlights

- **`notification.c` is excellent.** No heap allocation, clean helper decomposition, correct `strncpy`-plus-explicit-null pattern throughout, sequence-based eviction correctly implemented. 11 tests covering all edge cases including the exact regression scenario.

- **BLE/UI task decoupling is correct.** Posting typed `ble_evt_t` structs to a FreeRTOS queue with `pdMS_TO_TICKS(0)` (non-blocking) from the NimBLE host task is the right design. The UI task owns LVGL exclusively.

- **`display.c` DMA flush pattern is correct.** `on_color_trans_done` → `lv_display_flush_ready` (asynchronous) instead of synchronous flush. `lv_draw_sw_rgb565_swap` before bitmap draw handles ST7789 byte order correctly. Gap/offset configuration (`esp_lcd_panel_set_gap(panel, 34, 0)`) correctly centers the 172-pixel height in 240-pixel controller RAM.

- **`ble_service.c` JSON handling is thorough.** Malformed JSON, missing `action`, unknown `action`, missing required `id` are all handled with log + early return. Length bounds check on the write (`len > 512`) prevents stack overflow. `safe_strncpy` handles NULL source.

- **`protocol.py` is a clean pure-function module.** No side effects, all paths well-tested, `None` return for irrelevant events is an elegant filtering contract.

- **`_on_disconnect` thread safety is correct.** `call_soon_threadsafe(_clear_client)` with loop stored in `connect()` via `get_running_loop()` is the right pattern.

- **`socket_server.py` security is sound.** `0o600` permissions on the Unix socket, existing socket unlinked on start, socket removed on stop.

- **`_replay_active` snapshot is correct.** `list(self._active_notifications.values())` prevents `RuntimeError` on concurrent mutation during `asyncio.sleep` yields.

- **`clawd-notify` PID check is robust.** Handles stale PIDs, `ValueError` (corrupt file), `PermissionError`, `ProcessLookupError` — all real failure modes covered.

- **`install-hooks.sh` is safely conservative.** Outputs config for manual merge rather than writing it, avoiding silent overwrites of existing hook configurations.

---

## Recommendations (Priority Order)

1. **Check `ble_gatts_count_cfg` and `ble_gatts_add_svcs` return values** (issue #4) — silent GATT registration failure means the device advertises but is permanently non-functional. Wrap with `ESP_ERROR_CHECK`.

2. **Check `ble_hs_mbuf_to_flat` return value** (issue #3) — add error check before using `copied` to null-terminate `buf`.

3. **Check `lv_display_create` for NULL** (issue #5) — LVGL display creation failure currently causes NULL-pointer chaos in display setup.

4. **Check `xTaskCreate` return value** (issue #6) — silent UI task failure leaves the device advertising but non-functional.

5. **Wrap `_ble_sender` `ValueError` in try/except** (issue #9) — prevents the entire BLE sender from dying on an unknown event type.

6. **Add `-fsanitize=address,undefined` to test Makefile** (issue #14) — free safety net for the C unit tests.

7. **Document the `_lock_t` locking intent in `ui_manager.c`** (issue #23) — future maintainers need to know the lock covers both `rebuild_ui` and `lv_timer_handler`.
