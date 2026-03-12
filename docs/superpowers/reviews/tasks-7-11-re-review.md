# Re-Review: Python Host Fixes

**Date:** 2026-03-11
**Reviewer:** code-reviewer agent
**Original issues:** 1 critical, 5 high, 6 medium, 4 low
**Fixes claimed:** 9 issues addressed

---

## Verification of Fixes

### ✅ Critical #1 — `_replay_active` dict mutation: FIXED

```python
for msg in list(self._active_notifications.values()):
```
`list()` snapshot taken before iteration. ✓

### ✅ High #2 — `_on_disconnect` thread safety: FIXED

```python
def _on_disconnect(self, client: BleakClient) -> None:
    if self._loop is not None and self._loop.is_running():
        self._loop.call_soon_threadsafe(self._clear_client)
    else:
        self._clear_client()

def _clear_client(self) -> None:
    self._client = None
```

`_loop` is stored in `connect()` via `asyncio.get_running_loop()` — always the correct loop. `call_soon_threadsafe` correctly marshals the assignment back to the event loop thread. ✓

### ✅ High #3 — `_shutdown_event` moved to `__init__`: FIXED

Line 26: `self._shutdown_event = asyncio.Event()` in `__init__`. ✓

### ✅ High #4 — `get_event_loop()` → `get_running_loop()`: FIXED

Line 92: `loop = asyncio.get_running_loop()`. ✓

### ✅ High #5 — `session_id` uses `.get()`: FIXED

`protocol.py` lines 48, 56: `msg.get("session_id", "")`. ✓

Bonus: `protocol.py` also improved `project` extraction:
```python
project = Path(cwd).name if cwd else "unknown"
if not project:
    project = "unknown"
```
The second guard correctly handles `cwd="/"` (where `Path("/").name == ""`). Good defensive addition.

### ✅ High #6 — Socket leak fixed: FIXED

```python
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
try:
    sock.settimeout(3.0)
    sock.connect(str(SOCKET_PATH))
    sock.sendall(json.dumps(msg).encode("utf-8"))
except (...) as e:
    ...
finally:
    sock.close()
```
✓

### ✅ Medium #11 — Dead `DAEMON_MODULE` removed: FIXED ✓

### ✅ Medium #12 — `_connected` Event removed from `ble_client.py`: FIXED ✓

---

## New Issue Found: PID File Never Deleted — `daemon.py`

**Severity: High**

When fixing the "double `_remove_pid()`" issue, both calls were removed instead of just the duplicate. The `_remove_pid()` method still exists but is never called:

```python
def _remove_pid(self) -> None:
    if PID_PATH.exists():
        PID_PATH.unlink()   # this method is never called
```

Neither `_shutdown()` nor `run()` calls `_remove_pid()`. The PID file is written on startup but never cleaned up on exit. After the daemon stops, `is_daemon_running()` in `clawd-notify` reads the stale PID file and calls `os.kill(pid, 0)`. If that PID was reused by another process (common on long-running systems), `clawd-notify` will falsely believe the daemon is still running and skip starting it — permanently breaking notification delivery until the user manually deletes `~/.clawd/daemon.pid`.

**Fix:** Add `self._remove_pid()` to `_shutdown()` before returning:

```python
async def _shutdown(self) -> None:
    logger.info("Shutting down...")
    self._running = False
    self._shutdown_event.set()
    clear_payload = daemon_message_to_ble_payload({"event": "clear"})
    await self._ble.write_notification(clear_payload)
    await self._ble.disconnect()
    await self._socket.stop()
    self._remove_pid()   # ← add this back
```

---

## Minor Issue: Unused `os` Import in `protocol.py`

`import os` remains on line 4 of `protocol.py` but `os` is no longer used — `os.path.basename(cwd)` was replaced with `Path(cwd).name`. One-line cleanup: remove the `import os` line.

---

## Assessment: ❌ One fix incomplete

8 of 9 issues are cleanly resolved. One regression was introduced: the PID cleanup was accidentally dropped entirely. Fix is one line. Once that's added, this module is approved.
