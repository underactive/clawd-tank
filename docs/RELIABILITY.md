# Reliability

Requirements and practices for keeping Clawd Tank reliable across firmware, simulator, and host.

## Failure modes to handle

| Failure | Likelihood | Mitigation |
|---------|------------|------------|
| BLE connection drops | High | Daemon reconnects with backoff; device shows DISCONNECTED animation until reconnect; time is resynced on each `set_time` arrival |
| Mac sleep / wake | High | Daemon resyncs time and reconnects on wake; session staleness eviction runs on load |
| BLE MTU exceeded | Medium | JSON payloads must stay under 256 bytes; exceed → silent drop at the GATT layer (must be caught in tests) |
| Notification ring overflow | Medium | Max 8 notifications; oldest dropped on insert — this is by design, not a failure |
| ESP32 heap exhaustion | Low | ~200 KB free heap budget after IDF/BLE/LVGL; frame buffers lazy-allocated per slot; hard crash if exceeded |
| LVGL tick starvation | Low | `ui_manager` drives LVGL tick from a FreeRTOS task; animation frames missed but no crash |
| Daemon thread crash | Medium | Menu bar app has periodic health check; exceptions caught and logged, disconnected icon shown |
| Simulator subprocess hang | Low | Menu bar app sends `SIGKILL` on quit via `SimProcessManager` |
| Hook install drift across Claude Code versions | High | Menu bar app auto-updates hooks on startup when the installed version is outdated |
| `~/.clawd-tank/sessions.json` corruption | Low | Atomic write (temp + rename); malformed file → empty state on load, warning logged |

## Invariants

1. **BLE JSON parsing never crashes.** `parse_notification_json` in `ble_service.c` (firmware) and `sim_ble_parse.c` (simulator) must return a typed error code for malformed input and post no event to the queue.
2. **Every BLE action is parseable or dropped.** There is no partial processing. Unknown `action` values log a warning and are dropped.
3. **Frame buffers are lazy-allocated and freed on slot release.** No leaks across `MAX_SLOTS` worth of sessions.
4. **`clawd-tank-notify` uses stdlib only.** No third-party imports — it must run under whatever Python is first on the user's PATH.
5. **Session state is persisted atomically.** Write to `~/.clawd-tank/sessions.json.tmp`, rename over the real file. Never leave a truncated file.
6. **Time is re-synced on every reconnect.** The daemon always sends `set_time` immediately after a new BLE connection.
7. **Simulator and device see identical scene state.** The same firmware source compiles to both targets; any divergence is a bug in the shim layer.
8. **Subagent-active sessions are never evicted.** Staleness eviction explicitly skips sessions whose `agent_id` set is non-empty.

## Testing strategy

- **C unit tests** (`firmware/test/`, `make test`) — Notification ring buffer and config store. Fast, fixture-based.
- **Python tests** (`host/tests/`, pytest) — Protocol parsing, session state machine, transport multiplexing.
- **Simulator scenarios** (`simulator/scenarios/*.json`) — End-to-end flows exercised via `--headless --events` or the TCP bridge. Primary integration coverage without hardware.
- **BLE interactive tool** (`tools/ble_interactive.py`) — Manual verification of GATT server behavior on a real device.
- **Chaos fixtures** — When parsing logic changes, add a malformed-input test case (truncated JSON, unknown action, oversized payload) to the relevant suite.
