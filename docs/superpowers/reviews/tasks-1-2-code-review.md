# Code Review: Tasks 1–2 (Firmware Foundation)

**Date:** 2026-03-11
**Reviewer:** code-reviewer agent
**Scope:** ESP-IDF scaffold (Task 1) + notification data store (Task 2)
**Spec compliance:** pre-confirmed by spec-checker

---

## Code Review Summary

The scaffold is clean and minimal. The notification store is well-structured with a clear API and good separation between C logic and ESP-IDF. However, there is one real semantic bug in `notification.c`, one uninitialized lock in `main.c`, and a few missing safeguards in tests and the main task.

---

## Critical Issues ⚠️

### 1. `find_oldest` is not actually "insertion-order oldest" — `notification.c:30`

**The bug:** After dismissals, `find_free_slot` reuses freed lower indices. So slot 0 might contain a *newer* notification than slot 1, yet `find_oldest` always evicts the lowest active index. The overflow eviction policy silently violates the "drops oldest" contract.

**Reproducer (not covered by tests):**
```
add s0 → slot 0
add s1 → slot 1
dismiss s0        → slot 0 freed
add s2 → slot 0  (find_free_slot picks 0 first)
fill slots 2-7 with s3-s8
overflow: find_oldest picks slot 0 (s2, newest!) instead of slot 1 (s1, oldest)
```

**Fix options (all valid at this scale):**
- Add a `uint32_t seq` field to `notification_t` and evict the lowest seq.
- Use a circular buffer (head/tail indices).
- On dismiss, compact the array (shift items left) — keeps low indices always oldest.

---

### 2. `lvgl_lock` is never initialized — `main.c:11`

`static _lock_t lvgl_lock;` is declared but `_lock_init(&lvgl_lock)` is never called in `app_main`. Calling `_lock_acquire` on an uninitialized `_lock_t` is undefined behavior; on hardware this will corrupt state or crash.

**Additionally:** `_lock_t` is a newlib internal type. The idiomatic ESP-IDF approach is `SemaphoreHandle_t` + `xSemaphoreCreateMutex()`. LVGL's own ESP-IDF port examples use this pattern.

---

## High Priority Issues 🔴

### 3. `display_init()` return value unchecked — `main.c:34`

```c
lv_display_t *display = display_init();
xTaskCreate(ui_task, "ui_task", 4096, display, 5, NULL);
```

If `display_init()` returns NULL (e.g., SPI init fails), NULL is passed silently to the task. Add a null check and log + halt (`ESP_ERROR_CHECK` or explicit abort).

### 4. `xTaskCreate` return value unchecked — `main.c:36`

`xTaskCreate` returns `pdPASS` or `errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY`. A silent failure here means the UI never runs with no visible error. Should check and log a fatal error.

### 5. `lv_init()` appears to be missing

`app_main` calls `display_init()` and immediately uses LVGL objects, but `lv_init()` is never visibly called. If `display_init()` doesn't call it, this will crash on first LVGL call. Needs confirmation/explicit call in `app_main`.

---

## Medium Priority Issues 🟡

### 6. Redundant `active = false` before `memset` — `notification.c:82`

```c
store->items[idx].active = false;   // redundant
memset(&store->items[idx], 0, sizeof(notification_t));  // zeroes everything
```

The first line is a no-op since `memset` clears all fields including `active`. Remove it for clarity.

### 7. `count` is redundant derived state

`notification_store_t.count` must be kept in sync manually with `active` flags. For 8 slots, counting active items on demand (`for` loop over `NOTIF_MAX_COUNT`) is negligible cost and eliminates the drift risk. The `count` field also leaks internal state through the public header struct, tempting callers to read it directly rather than calling `notif_store_count()`.

### 8. Overflow test doesn't exercise the `find_oldest` bug — `test_notification.c:65`

`test_overflow_drops_oldest` fills the store from empty in slot order, then overflows. This happens to pass because slot 0 *is* the oldest in that scenario. A test that dismisses slot 0 first, refills, then overflows would catch the eviction bug.

---

## Low Priority Suggestions 🔵

### 9. `find_oldest` name is misleading

It returns "lowest active index", not "insertion-order oldest". Once the eviction bug is fixed (with sequence numbers or compaction), the name and implementation should match.

### 10. Missing test cases

- String truncation at exact boundary (`NOTIF_MAX_ID_LEN - 1`, `NOTIF_MAX_MSG_LEN - 1`)
- Dismiss from empty store (returns -1, doesn't crash)
- `get` with negative index
- Calling `add` with empty-string ID

### 11. Test Makefile missing sanitizers

```makefile
CFLAGS = -Wall -Wextra -I../main
```

Add `-fsanitize=address,undefined -Werror` for host tests. ASan would catch any out-of-bounds writes in `write_slot` or `memset`. Low cost, high signal.

---

## Positive Highlights ✨

- **`notification.c` structure is clean.** Three focused private helpers (`find_by_id`, `find_free_slot`, `find_oldest`) each do exactly one thing. `write_slot` consolidates the safe `strncpy` + null-termination pattern correctly in one place — no risk of buffer overruns.
- **No heap allocation.** All storage is on the stack/static. Correct choice for embedded.
- **`strncpy` with explicit null termination.** `slot->id[NOTIF_MAX_ID_LEN - 1] = '\0'` after every `strncpy` — no truncation bug risk.
- **`notification.h` API is well-documented.** Return value semantics (0/-1, idempotent dismiss) are spelled out in comments.
- **Pure C module with no ESP-IDF deps.** Host-testable by design. This is the right call.
- **`sdkconfig.defaults` disables unused BLE roles.** Reduces attack surface and binary size.
- **`Kconfig.projbuild` WiFi default-off.** Correct for a BLE-only device.
- **Test Makefile is correct and minimal.** `-Wall -Wextra` is a good baseline.

---

## Recommendations

1. **Fix the `find_oldest` eviction bug** — this is a correctness issue that will produce confusing behavior in production. The simplest fix is a monotonic `uint32_t seq` counter in `notification_store_t`, incremented on each `add`, stored per-notification, and evicting the minimum.

2. **Fix the uninitialized lock in `main.c`** — this will crash on hardware. Either call `_lock_init` or switch to `SemaphoreHandle_t` (preferred for ESP-IDF).

3. **Add null checks for `display_init()` and `xTaskCreate()`** — these are stubs now but the guards should be in place before they get real implementations.

4. **Add one overflow test after a dismiss** to cover the eviction path post-compaction.

5. **Consider adding `-fsanitize=address,undefined` to the test Makefile** — free to do now, catches bugs early.
