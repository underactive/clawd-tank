# Code Review: Tasks 3–6 (Remaining Firmware)

**Date:** 2026-03-11
**Reviewer:** code-reviewer agent
**Scope:** display.h/c, ble_service.h/c, ui_manager.h/c, main.c (Task 6)
**Spec compliance:** pre-confirmed by spec-checker
**Note:** notification.c eviction bug being fixed separately.

---

## Code Review Summary

The firmware integration is well-structured with clear ownership per module. The display driver, BLE service, and UI manager each have a single responsibility and clean interfaces. The main.c wiring is significantly improved over the Task 1 stub. However, the uninitialized `_lock_t` bug from the original stub has migrated into `ui_manager.c` and must be fixed, and there are several unchecked return values that will cause silent failures on hardware.

---

## Critical Issues ⚠️

### 1. `_lock_t s_lock` uninitialized in `ui_manager.c:18`

```c
static _lock_t s_lock;   // never initialized
...
void ui_manager_init(void) {
    notif_store_init(&s_store);
    // _lock_init(&s_lock) is missing here
    ...
```

`_lock_acquire(&s_lock)` is called in both `ui_manager_handle_event` and `ui_manager_tick`, but `_lock_init` is never called. This is the same bug identified in the Task 1 review — it has moved from `main.c` to `ui_manager.c`. Operating on an uninitialized `_lock_t` is undefined behavior and will corrupt state or crash on hardware.

As noted previously, the correct fix is either:
- Call `_lock_init(&s_lock)` in `ui_manager_init()`
- Switch to `SemaphoreHandle_t` + `xSemaphoreCreateMutex()` (idiomatic ESP-IDF)

---

## High Priority Issues 🔴

### 2. `buf[96]` too small for max-length notification row — `ui_manager.c:82`

```c
char buf[96];
snprintf(buf, sizeof(buf), "> %s\n  %s", n->project, n->message);
```

`project` can be up to 31 chars (`NOTIF_MAX_PROJ_LEN - 1`), `message` up to 63 chars (`NOTIF_MAX_MSG_LEN - 1`). The fixed prefix adds 5 chars (`"> "`, `"\n"`, `"  "`). Total: 31 + 63 + 5 + 1 (null) = 100 bytes. `buf` is 96. `snprintf` truncates safely, but the last 4 characters of the message will be silently dropped on max-length inputs. Increase to at least 100 (104 is a clean round number with margin).

### 3. `ble_evt_t` field sizes hardcoded instead of using `NOTIF_MAX_*_LEN` — `ble_service.h:19-21`

```c
typedef struct {
    ble_evt_type_t type;
    char id[48];        // should be char id[NOTIF_MAX_ID_LEN]
    char project[32];   // should be char project[NOTIF_MAX_PROJ_LEN]
    char message[64];   // should be char message[NOTIF_MAX_MSG_LEN]
} ble_evt_t;
```

These values match `notification.h` constants today, but they're not linked. If `NOTIF_MAX_ID_LEN` changes, `ble_evt_t` silently uses the wrong size. `ble_service.h` should `#include "notification.h"` and use the constants directly.

### 4. `xQueueSend` with 100ms timeout from NimBLE host task — `ble_service.c:89,149,157`

```c
xQueueSend(s_evt_queue, &evt, pdMS_TO_TICKS(100));
```

This is called from the NimBLE host task (a FreeRTOS task running the BLE stack). Blocking the NimBLE host task for 100ms while the queue is full can cause the BLE stack to miss events or timeout. The UI task drains the queue every 5ms under normal conditions, but defensive design should avoid blocking the BLE stack at all. Use `pdMS_TO_TICKS(0)` (non-blocking) and log a warning if the send fails.

### 5. `gpio_config()` return value not checked — `display.c:65`

```c
gpio_config(&bl_cfg);   // returns esp_err_t, ignored
```

A backlight GPIO init failure is silent. Use `ESP_ERROR_CHECK(gpio_config(&bl_cfg))`.

### 6. `ble_gap_adv_start` return value not checked — `ble_service.c:139`

```c
ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                  &adv_params, ble_gap_event_cb, NULL);
```

If advertising fails (e.g., invalid address, stack not ready), the device never becomes discoverable and there's no log. Check the return value and log on failure.

---

## Medium Priority Issues 🟡

### 7. `ble_hs_mbuf_to_flat` return value not checked — `ble_service.c:103`

```c
ble_hs_mbuf_to_flat(ctxt->om, buf, len, &copied);
buf[copied] = '\0';
```

`ble_hs_mbuf_to_flat` returns `int` (OS error code). On failure, `copied` may be 0 or indeterminate, and `buf[copied] = '\0'` could write an uninitialized string to the JSON parser. Check the return value.

### 8. `ble_gatts_count_cfg` / `ble_gatts_add_svcs` return values not checked — `ble_service.c:190-191`

```c
ble_gatts_count_cfg(gatt_svcs);
ble_gatts_add_svcs(gatt_svcs);
```

Both return `int` error codes. A malformed GATT configuration silently fails to register the service. Wrap with `ESP_ERROR_CHECK` or explicit checks.

### 9. `lv_display_create` return value not checked — `display.c:116`

```c
lv_display_t *display = lv_display_create(LCD_H_RES, LCD_V_RES);
// no NULL check
```

On LVGL memory exhaustion this returns NULL. All subsequent LVGL calls with a NULL display will produce undefined behavior. Add a null check and `ESP_ERROR_CHECK`-style abort.

### 10. `xTaskCreate` return value not checked — `main.c:48`

```c
xTaskCreate(ui_task, "ui_task", 8192, NULL, 5, NULL);
```

If heap is exhausted, the UI task silently never starts. Check the return value against `pdPASS` and log a fatal error.

### 11. `UI_STATE_NOTIFICATION` appears vestigial — `ui_manager.c:51-65`

`UI_STATE_NOTIFICATION` is set on `BLE_EVT_NOTIF_ADD` (line 119) but `update_state()` (which runs on dismiss/clear) never sets it — only `UI_STATE_IDLE` or `UI_STATE_LIST`. The `rebuild_ui` switch treats `UI_STATE_NOTIFICATION` and `UI_STATE_LIST` identically (same case fallthrough). So `UI_STATE_NOTIFICATION` exists only transiently, and has no unique behavior or rendering. It should either be given distinct behavior (e.g., flash/pulse the new notification) or collapsed into `UI_STATE_LIST`.

### 12. DMA buffer failures use bare `assert` — `display.c:122`

```c
assert(buf1 && buf2);
```

`assert` can be compiled out with `NDEBUG`. For a hard dependency like DMA render buffers, prefer `configASSERT` (FreeRTOS-idiomatic, respects `configASSERT` definition) or an explicit `if (!buf1 || !buf2) { ESP_LOGE(...); abort(); }`.

---

## Low Priority Suggestions 🔵

### 13. `ble_on_sync` missing `ble_hs_util_ensure_addr(0)` — `ble_service.c:170`

Best practice for NimBLE on ESP32 is to call `ble_hs_util_ensure_addr(0)` in the sync callback before starting advertising to ensure a valid public address is available. Some ESP32 variants behave unexpectedly without it.

### 14. `display_init()` return value discarded in `main.c:42` — no comment

```c
display_init();   // return value silently discarded
```

`display_init()` returns `lv_display_t*`, but since LVGL registers it as the default display internally, `ui_manager_init()` can call `lv_screen_active()` without the handle. This is valid but surprising to readers. A comment like `// return value not needed; LVGL tracks default display internally` removes the ambiguity.

### 15. `ble_svc_gap_device_name_set` return value not checked — `ble_service.c:186`

Returns `int`. Silent failure means the device advertises without a name. Low risk but cheap to check.

### 16. `assert` vs `configASSERT` for queue creation — `main.c:39`

```c
assert(s_evt_queue);
```

`configASSERT` is the FreeRTOS-idiomatic alternative and respects project-level assert configuration.

### 17. `io_handle` and `panel` not stored at module level — `display.c:80,93`

These are currently local variables. For a device that never deinitializes, this is fine. If teardown is ever needed (e.g., deep sleep with display off), they'll need to be module globals. Worth a comment noting they're intentionally ephemeral.

---

## Positive Highlights ✨

- **`display.c` DMA flush pattern is correct.** Using `on_color_trans_done` callback to call `lv_display_flush_ready()` asynchronously (rather than synchronously in `lvgl_flush_cb`) is the right double-buffered DMA approach. Pixel endianness swap with `lv_draw_sw_rgb565_swap` before bitmap draw is also correct for ST7789.
- **`display_init()` calls `lv_init()`** — the missing init issue from Task 1 is fixed. Backlight-off during init is good practice.
- **`safe_strncpy` in `ble_service.c`** handles NULL source gracefully. All string copies in `parse_notification_json` correctly use it.
- **JSON validation in `ble_service.c` is thorough.** Malformed JSON, missing action field, unknown action, and missing required `id` field are all logged and rejected cleanly. The characteristic correctly supports both `BLE_GATT_CHR_F_WRITE` and `BLE_GATT_CHR_F_WRITE_NO_RSP`, matching the Python client's `response=False`.
- **Write length bounds check in `notification_write_cb`** (`len > 512`) prevents oversized payloads from overflowing the stack buffer.
- **`main.c` (Task 6) is a significant improvement over the Task 1 stub.** Clean wiring of three modules, correct ordering (display → BLE → UI task), queue-based decoupling of BLE and UI tasks.
- **BLE event queue design is clean.** Posting typed `ble_evt_t` structs to a FreeRTOS queue is the right pattern for safely crossing the NimBLE/LVGL task boundary.
- **`update_state()` correctly guards against transitioning out of `DISCONNECTED`** (`ui_manager.c:92`): won't flip to IDLE on a dismiss while disconnected.
- **`notify_lvgl_flush_ready` returns `false`** — correct: DMA buffers are not to be freed by the driver.
- **ST7789 gap/offset config** (`esp_lcd_panel_set_gap(panel, 34, 0)`) correctly accounts for the 172-pixel height being centered in the 240-pixel controller RAM.

---

## Recommendations

1. **Fix the uninitialized `s_lock`** in `ui_manager_init()` (issue #1) — same critical fix as Task 1 review; call `_lock_init(&s_lock)` or switch to `SemaphoreHandle_t`. This will crash on hardware.
2. **Fix `buf[96]` to at least `100`** in `rebuild_ui` (issue #2) — one-line change, prevents silent display truncation.
3. **Link `ble_evt_t` sizes to `NOTIF_MAX_*_LEN` constants** (issue #3) — include `notification.h` in `ble_service.h` and replace the hardcoded values.
4. **Change `xQueueSend` timeouts to 0ms** in BLE callbacks (issue #4) — protects the NimBLE host task from blocking.
5. **Add `ESP_ERROR_CHECK` to `gpio_config`, `ble_gap_adv_start`, `ble_gatts_count_cfg`, `ble_gatts_add_svcs`** (issues #5, #6, #8) — cheap guards against silent hardware failures.
