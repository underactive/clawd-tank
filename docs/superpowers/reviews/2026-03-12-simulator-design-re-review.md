# Simulator Design Spec Re-Review
**Spec:** `docs/superpowers/specs/2026-03-12-simulator-design.md`
**Reviewer:** Claude Sonnet 4.6
**Date:** 2026-03-12
**Type:** Follow-up review verifying fixes for 6 previously identified blocking issues

---

## Verdict: APPROVED

All 6 blocking issues from the previous review have been correctly resolved. No new blocking issues were introduced. Two minor observations are noted below but neither blocks implementation.

---

## Fix Verification

### Issue 1 — `_lock_t` shim moved to `freertos/FreeRTOS.h`

**Fix applied:** The spec now places the `_lock_t` typedef and `_lock_*` no-op macros directly inside `shims/freertos/FreeRTOS.h`. The `shims/sys/lock.h` entry has been removed from the directory tree.

**Verification:** Correct. `ui_manager.c` uses `_lock_t s_lock` (line 31), `_lock_init` (line 117), `_lock_acquire` (lines 143, 210), and `_lock_release` (lines 203, 241). The include chain is `ui_manager.h -> ble_service.h -> freertos/FreeRTOS.h`, which is exactly the shim that now defines `_lock_t`. The macOS system `sys/lock.h` is no longer in the load path for this type, eliminating the shadow problem entirely.

**Status: RESOLVED**

---

### Issue 2 — `ble_service.h` shim now uses `notification.h` constants

**Fix applied:** The shim now opens with `#include "notification.h"` and uses `NOTIF_MAX_ID_LEN`, `NOTIF_MAX_PROJ_LEN`, `NOTIF_MAX_MSG_LEN` for struct field sizes.

**Verification:** Correct and consistent with real firmware. The real `ble_service.h` (line 7) includes `notification.h` and uses the same three constants for the `ble_evt_t` struct fields (lines 20-22). The shim struct layout will always match the real one regardless of future constant changes.

Cross-check: `notification.h` defines `NOTIF_MAX_ID_LEN 48`, `NOTIF_MAX_PROJ_LEN 32`, `NOTIF_MAX_MSG_LEN 64`. The shim's `ble_evt_t` now uses those, matching the real struct exactly.

**Status: RESOLVED**

---

### Issue 3 — `lv_timer_handler()` single-call clarified

**Fix applied:** The spec now explicitly documents in the "Interactive mode" section that `ui_manager_tick()` is where the single `lv_timer_handler()` call lives, and that `sim_display_tick()` must NOT call `lv_timer_handler()` — it only pumps SDL events.

**Verification:** The spec text (lines 72-73) correctly identifies the ownership rule: one call per loop iteration, owned by `ui_manager_tick()`. Cross-checking `ui_manager.c` confirms `lv_timer_handler()` is called exactly once, at line 239, inside `ui_manager_tick()`. The LVGL SDL driver registers a timer internally but does not auto-call `lv_timer_handler()` from outside — that call is entirely in user code. The clarification correctly prevents an implementer from adding a second call in `sim_display_tick()`.

One note: the spec does not address whether LVGL's SDL driver registers a self-referential timer that would call `lv_timer_handler()` recursively. It does not — the SDL event handler timer runs as a normal LVGL timer polled by `lv_timer_handler()`, not as a re-entrant call. The single-call guarantee holds.

**Status: RESOLVED**

---

### Issue 4 — `lv_tick_set_cb()` added for headless simulated time

**Fix applied:** The spec now specifies that in headless mode, step 3 of the initialization sequence is `if headless: lv_tick_set_cb(sim_get_tick)`, and that `sim_get_tick` returns a monotonically increasing counter advanced by `wait` commands.

**Verification:** Correct mechanism. `lv_tick_set_cb()` signature is `void lv_tick_set_cb(lv_tick_get_cb_t cb)` where `lv_tick_get_cb_t` is `typedef uint32_t (*)(void)`. The spec's `sim_get_tick` function matches this signature (returns `uint32_t`, no parameters). When set, all calls to `lv_tick_get()` in `scene_tick()`, `ui_manager_tick()` sleep timeout check, and LVGL animation system will return the simulated counter. The `wait <N>` description (lines 80-81) correctly notes that simulated time is advanced in 33ms steps via repeated `lv_timer_handler()` calls, which will advance LVGL animations and fire timers at the right simulated moments.

Cross-check against `ui_manager.c`: the sleep timeout computes `lv_tick_get() - s_last_activity_tick >= SLEEP_TIMEOUT_MS` (lines 217-218). With `lv_tick_set_cb()` active in headless mode, this will fast-forward correctly — a `wait 300000` will trigger the 5-minute sleep animation as expected.

**Status: RESOLVED**

---

### Issue 5 — `dismiss` index-to-ID mapping specified

**Fix applied:** The spec now states that `sim_events.c` maintains an ordered list of injected notification IDs (appended on each `notify` command), and `dismiss N` maps to `id_list[N]` to produce the correct `ble_evt_t.id`. The scenario JSON format also uses `"index": 0` with the same semantics.

**Verification:** Correct design. `ui_manager_handle_event()` for `BLE_EVT_NOTIF_DISMISS` calls `notif_store_dismiss(&s_store, evt->id)` (line 182), which is a string-keyed lookup. The index-to-ID mapping in `sim_events.c` is the correct shim layer to absorb this impedance. The spec's description is precise enough to implement: maintain a `char id_list[N][NOTIF_MAX_ID_LEN]` ordered list, append on `notify`, index into it on `dismiss`.

One edge case worth noting: the spec does not specify behavior if `dismiss N` is called with an out-of-bounds index, or after that notification has already been dismissed (the ID still exists in the list but `notif_store_dismiss` will return -1 for a missing ID). This is an implementation detail rather than a spec gap — `notif_store_dismiss` is already documented as idempotent (returns -1 if not found, no side effect), so the runtime behavior is safe. The list should not shrink on dismiss, only on `clear`, to preserve stable indices. This is implied but not stated; however, it is clear enough from context and not a blocker.

**Status: RESOLVED**

---

### Issue 6 — `--run-ms` default defined

**Fix applied:** The CLI summary now specifies: `Default: (time of last event or last wait completion) + 500ms`.

**Verification:** The rule is unambiguous. "Time of last event or last wait completion" covers both the `--events` inline case (last token in the semicolon-separated string) and the `--scenario` JSON case (highest `time_ms` value). The +500ms grace period is sufficient for LVGL to process the final frame and write any pending screenshots before the process exits.

**Status: RESOLVED**

---

## New Issues Introduced

None. The fixes are additive (text added, no existing correct text removed or contradicted). The shim struct, init sequence, tick model, and event handling descriptions are internally consistent and consistent with the firmware source.

---

## Minor Observations (non-blocking)

### A. `dismiss` list-shrink behavior not specified

As noted in Issue 5 verification: the spec does not explicitly state that the `sim_events.c` ID list should not shrink on `dismiss` (i.e., indices remain stable after a dismiss). The safe assumption is that the list is append-only with per-slot valid flags, but an implementer might reasonably compact the list. This could cause `dismiss 1` after `dismiss 0` to hit the wrong notification.

Recommendation: add one sentence: "The ID list is append-only; indices are not recomputed after a dismiss."

This is low priority — the agent development loop described in the spec typically uses short, linear event sequences where this ambiguity does not arise.

---

### B. Headless mode and `sim_display_tick()` as no-op

The initialization sequence (line 139) shows `sim_display_tick()` in the headless branch with a note "or no-op (headless)". The main loop pseudocode calls it unconditionally. The spec correctly handles this by noting it is a no-op in headless mode, but it is slightly ambiguous whether the time-advance logic (SDL_Delay vs. advance sim time, line 143) lives inside `sim_display_tick()` or in `sim_main.c`'s loop directly. Either placement works, but the implementer will need to decide. Not a blocker.

---

## Summary

| Issue | Fix | Verified Against Firmware | Status |
|-------|-----|--------------------------|--------|
| 1. `_lock_t` in FreeRTOS.h | Correct placement, no sys/lock.h dependency | `ui_manager.c` lines 31, 117, 143, 203, 210, 241 | RESOLVED |
| 2. `ble_service.h` uses notification.h constants | `#include "notification.h"` + constants used | Real `ble_service.h` lines 7, 20-22; `notification.h` lines 9-11 | RESOLVED |
| 3. Single `lv_timer_handler()` call | SDL tick path clarified as SDL-only, no lv_timer_handler | `ui_manager.c` line 239 is the sole call site | RESOLVED |
| 4. `lv_tick_set_cb()` in headless init | Step 3 of init sequence added | `lv_tick.h` line 84 confirms API exists | RESOLVED |
| 5. dismiss index-to-ID mapping | ID list bookkeeping in sim_events.c specified | `ui_manager.c` line 182 confirms string-ID lookup | RESOLVED |
| 6. `--run-ms` default | "+500ms after last event/wait" explicitly stated | N/A (implementation contract) | RESOLVED |
