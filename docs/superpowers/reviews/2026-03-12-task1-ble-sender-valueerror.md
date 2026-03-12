# Code Review: Task 1 — Guard `_ble_sender` against ValueError from unknown events

**Reviewed:** 2026-03-12
**Base:** 88a637e1d2c95616513d061ba6b7680c9800c067
**Head:** bddd6d4b44fb92aa749b8b5c3de21ead6515e7e5
**Files changed:** `host/clawd_tank_daemon/daemon.py`, `host/tests/test_daemon.py`
**Test suite:** 26/26 passed

---

## Code Review Summary

This change adds a `try/except ValueError` guard around `daemon_message_to_ble_payload` in the `_ble_sender` loop, plus two new tests that confirm the fix. The scope is exactly right — a 7-line production change and 38 lines of tests. Files stay small and maintain clear single responsibilities. The fix is correct and closes the crash path described in the plan.

---

## Critical Issues

None.

---

## High Priority Issues

None.

---

## Medium Priority Issues

**1. `_replay_active` has the same unguarded call — and the fix doesn't cover it (daemon.py line 44)**

`_replay_active` calls `daemon_message_to_ble_payload` without a `try/except`:

```python
async def _replay_active(self) -> None:
    for msg in list(self._active_notifications.values()):
        payload = daemon_message_to_ble_payload(msg)   # <-- unguarded
```

In practice this is unlikely to trigger today, because `_handle_message` only stores `add` events in `_active_notifications`, and `add` is a known event. But the reasoning is fragile: if that invariant ever changes (e.g., a future developer stores `clear` messages in the active set, or the set is populated from a persisted state file), `_replay_active` will crash the sender loop on reconnect — the exact same failure mode as the bug just fixed.

Recommended fix: apply the same guard pattern, or extract a shared helper:

```python
async def _replay_active(self) -> None:
    logger.info("Replaying %d active notifications", len(self._active_notifications))
    for msg in list(self._active_notifications.values()):
        try:
            payload = daemon_message_to_ble_payload(msg)
        except ValueError:
            logger.error("Skipping unknown event during replay: %s", msg.get("event"))
            continue
        await self._ble.write_notification(payload)
        await asyncio.sleep(0.05)
```

**2. `_ble_sender` calls `ensure_connected` before confirming the message is valid (daemon.py lines 75-81)**

The current execution order is:
1. Dequeue message
2. `await self._ble.ensure_connected()` — potentially triggers BLE reconnect/replay
3. Try to convert the message; skip if ValueError

If the message is unknown, step 2 already ran. That is not wrong today, but it means a flood of unknown events would trigger repeated `ensure_connected` calls (and potentially `_replay_active` calls) before being discarded. The guard could be moved before `ensure_connected`:

```python
try:
    payload = daemon_message_to_ble_payload(msg)
except ValueError:
    logger.error("Skipping unknown event: %s", msg.get("event"))
    continue

await self._ble.ensure_connected()
success = await self._ble.write_notification(payload)
```

This is a small improvement in efficiency and makes the intent clearer: validate, then connect.

---

## Low Priority Suggestions

**3. `test_unknown_event_does_not_crash_sender` is more of a unit test for `protocol.py` than a test of `_handle_message` behavior (test_daemon.py lines 83-93)**

The test does two distinct things: it verifies that `_handle_message` enqueues unknown events (good), and it separately asserts that `daemon_message_to_ble_payload` raises `ValueError` on them (this is already tested in `test_protocol.py::test_ble_payload_unknown_event_raises`). The cross-module assertion inside a daemon test adds a subtle coupling — if `protocol.py` is refactored to return `None` instead of raising, this test will fail for a reason unrelated to what its docstring describes.

Consider splitting: keep the queue-size assertion in this test and remove the `pytest.raises` block, which belongs only in `test_protocol.py`.

**4. `test_ble_sender_skips_unknown_event` uses `asyncio.sleep(0.1)` as a drain mechanism (test_daemon.py line 108)**

Time-based waits are fragile on slow CI machines. A more robust approach is to drain the queue explicitly before cancelling:

```python
await daemon._pending_queue.join()  # requires task_done() calls in _ble_sender
```

`asyncio.Queue` supports `task_done()` / `join()` for exactly this purpose. Alternatively, the test could poll `daemon._ble.write_notification.call_count` with a short timeout loop. The current `sleep(0.1)` works in practice for a local queue with no I/O, but it is worth noting.

This also requires `_pending_queue.task_done()` to be called inside `_ble_sender` after each item is processed, which is currently absent and would also fix a subtle correctness gap if `asyncio.Queue.join()` is ever used elsewhere.

**5. The `assert ... >= 1` bound is loose (test_daemon.py line 116)**

The test puts exactly one valid message (`dismiss`) in the queue after the bogus one. The assertion `write_notification.call_count >= 1` is correct, but `== 1` would be more precise and would catch any accidental double-send regression. Given that the test controls the queue contents exactly, prefer the tighter bound.

---

## Positive Highlights

- The fix is minimal and surgical. Exactly one call site is guarded, the error is logged at the right level (`error`), the loop continues — all exactly right.
- The commit message is accurate and explains the failure mode clearly.
- `test_ble_sender_skips_unknown_event` correctly stubs the entire BLE layer with `AsyncMock` and tests the sender loop end-to-end rather than just the conversion function. This is the right level of integration for this kind of async loop test.
- File sizes remain appropriate: `daemon.py` is 128 lines, `test_daemon.py` is 117 lines. No bloat introduced.
- Responsibility boundaries are respected: the fix lives in the transport layer (`daemon.py`) and leaves `protocol.py` unchanged. The protocol still raises on unknown events (correct), and the daemon handles the exception at the right boundary.

---

## Recommendations

The two medium-priority issues should be addressed before this chunk is considered fully hardened:

1. Guard `_replay_active` with the same `try/except ValueError` — it is the only other call site of `daemon_message_to_ble_payload` in the module.
2. Optionally reorder `ensure_connected` to after the validation check in `_ble_sender`.

The low-priority suggestions (especially the `task_done()` / `join()` pattern) are improvements worth tracking but not blocking.
