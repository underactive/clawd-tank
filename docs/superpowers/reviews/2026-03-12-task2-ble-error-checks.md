# Code Review: Task 2 — Add Error Checks to ble_service.c

**Reviewer:** Claude Sonnet 4.6
**Date:** 2026-03-12
**Base SHA:** bddd6d4b44fb92aa749b8b5c3de21ead6515e7e5
**Head SHA:** d254aed13af73855ae6bde7a319a7ab86dec61a3
**File reviewed:** `firmware/main/ble_service.c`

---

## Code Review Summary

This change adds four error checks to `ble_service.c` that were previously silently ignored. The diff is small (24 lines added), mechanical, and directly follows the plan in `docs/superpowers/plans/2026-03-12-hardening-and-hooks.md`. The implementation is correct and faithfully matches the plan spec. Two questions are worth discussing: the severity level chosen for `ble_svc_gap_device_name_set` failure, and a pre-existing unchecked call to `ble_gap_adv_set_fields` that this task did not address (minor, but worth noting for completeness).

---

## Critical Issues

None.

---

## High Priority Issues

None.

---

## Medium Priority Issues

### 1. `ble_gap_adv_set_fields` return value is still unchecked in `start_advertising` (pre-existing, not introduced here)

`start_advertising` at line 141 calls `ble_gap_adv_set_fields(&fields)` without checking its return value. This function returns `int` (confirmed in `ble_gap.h`). If the advertising payload is too large or malformed, the call silently fails and `ble_gap_adv_start` is then called with an empty/invalid payload, making the device invisible to the host without any log output.

This was not introduced by this task, but since the task is explicitly a hardening pass over `ble_service.c`, it is an oversight that leaves a gap in the coverage. The plan's Step 4 commit message lists four items but doesn't mention this one.

Suggested fix (consistent with the existing pattern in `start_advertising`):

```c
int rc = ble_gap_adv_set_fields(&fields);
if (rc != 0) {
    ESP_LOGW(TAG, "Failed to set adv fields: %d", rc);
    return;
}
```

---

## Low Priority Suggestions

### 2. `ble_on_sync` failure returns silently — no retry path

When `ble_hs_util_ensure_addr(0)` fails and `ble_on_sync` returns early, advertising never starts and the device goes dark with no recovery mechanism. The NimBLE reference example (`apps/bleprph/src/main.c`) uses `assert(rc == 0)` here instead of a graceful return, treating this as a fatal misconfiguration. On an embedded device with no operator, a silent return may actually be worse than `abort()` followed by a watchdog reboot, because `abort()` would at least trigger the esp-idf panic handler and potentially restart.

This is a judgment call — the current `LOGE` + `return` matches the plan spec exactly and is not wrong. But the team should be aware that if `ensure_addr` fails at runtime, the device will be stuck in a BLE-less state with no self-recovery unless the watchdog fires. Given that this is a notification device (its only job is BLE + display), escalating to `abort()` or `ESP_ERROR_CHECK`-style handling here may be preferable in production.

### 3. `ble_svc_gap_device_name_set` failure logged at LOGW but continues — severity is appropriate but the consequence deserves a comment

The `LOGW` level for `ble_svc_gap_device_name_set` failure is correct: NimBLE will fall back to the compiled-in default name (`CONFIG_BT_NIMBLE_SVC_GAP_DEVICE_NAME`), so the device is still functional. However, a brief inline comment would make the intent clear to future readers, since the code continues without the name being set:

```c
int rc = ble_svc_gap_device_name_set("Clawd Tank");
if (rc != 0) {
    // Non-fatal: NimBLE falls back to the Kconfig default name.
    ESP_LOGW(TAG, "Failed to set GAP device name: %d", rc);
}
```

In practice this call can only fail if `"Clawd Tank"` (10 chars) exceeds `BLE_SVC_GAP_DEVICE_NAME_MAX_LENGTH` — which defaults to 31 in NimBLE, so this branch is unreachable with the current device name. The check is still good practice since the name could change.

---

## Positive Highlights

- **`abort()` for GATT registration failures is exactly right.** `ble_gatts_count_cfg` and `ble_gatts_add_svcs` failures indicate a build-time misconfiguration of the `gatt_svcs` table. There is no meaningful recovery at runtime; `abort()` causes the esp-idf panic handler to fire and leaves a diagnostic trace in flash. Using `LOGE` before `abort()` is the idiomatic ESP-IDF pattern and matches how `display.c` handles fatal init failures elsewhere in the codebase (`assert` on DMA buffer alloc).

- **`ble_hs_mbuf_to_flat` returning `BLE_ATT_ERR_UNLIKELY` is the correct ATT error code.** Returning a non-zero ATT error from a GATT write callback causes NimBLE to send an ATT Error Response PDU to the peer, which is far better than proceeding with a partially-copied buffer. The choice of `BLE_ATT_ERR_UNLIKELY` ("unlikely error") is the right generic code when the failure is internal rather than application-logic.

- **`ble_hs_util_ensure_addr(0)` placement is correct.** Calling it inside `ble_on_sync` (the NimBLE sync callback) is the canonical pattern shown in the `bleprph` reference application. Doing it inside `ble_service_init` would be too early — the BLE host stack is not yet running.

- **Variable reuse (`rc`) in `ble_service_init` is clean.** Declaring `int rc` once and reusing it for three successive calls avoids redundant declarations while keeping each check visually tight.

- **Error severity is consistent with the rest of the file.** `LOGW` for recoverable/non-fatal cases, `LOGE` + `abort()` for fatal misconfigurations. This matches the pre-existing pattern (`LOGW` for queue-full events, `LOGE` for ensure_addr).

---

## Recommendations

1. Add the missing `ble_gap_adv_set_fields` return check in `start_advertising` to complete the hardening coverage for this file. This is a one-liner and would close the last unchecked `int`-returning call in `ble_service.c`.

2. Consider documenting the `ble_on_sync` early-return behaviour (or escalating to `abort()`) so future maintainers understand the device's failure mode when no BLE address is available.

3. The four changes introduced in this task are all correct and follow ESP-IDF/NimBLE conventions. The change is ready to merge as-is; the above items are improvements, not blockers.
