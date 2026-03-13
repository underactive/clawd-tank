# Simulator-Daemon Bridge Design

**Date:** 2026-03-13
**Status:** Approved

## Problem

The simulator and daemon are disconnected. The simulator only accepts events from CLI arguments or keypresses, while the daemon communicates exclusively over BLE to real hardware. There is no way to test the full Claude Code hooks → daemon → display pipeline without physical ESP32 hardware.

## Solution

Add a TCP socket bridge between the simulator and daemon, so the daemon can drive the simulator using the same protocol it uses for BLE. Both transports (BLE and TCP) run simultaneously when `--sim` is passed; `--sim-only` disables BLE for quieter development.

## Architecture

```
Claude Code hooks → clawd-tank-notify → Unix socket → daemon
                                                        ├── BLE → ESP32 (existing)
                                                        └── TCP:19872 → simulator (new)
```

## Simulator: TCP Socket Listener

### New module: `sim_socket.c` / `sim_socket.h`

Activated with `--listen [port]` (default port: `19872`).

- Spawns a **background pthread** that binds (`SO_REUSEADDR`), listens, and accepts one TCP client at a time. On bind failure, prints a clear error and exits.
- The socket thread does **not** call `ui_manager_handle_event()` directly (LVGL is not thread-safe). Instead, it pushes `ble_evt_t` structs into a **thread-safe queue** (mutex-guarded ring buffer), mirroring the firmware's `xQueueSend` pattern.
- The **main loop** drains this queue each tick before calling `ui_manager_tick()`, via a new `sim_socket_process()` function.
- On TCP client connect → enqueues `BLE_EVT_CONNECTED`
- On TCP client disconnect → enqueues `BLE_EVT_DISCONNECTED`
- Reads **newline-delimited JSON** from the TCP socket

**Graceful shutdown:** `sim_socket_shutdown()` sets a flag and calls `shutdown(listen_fd, SHUT_RDWR)` to unblock any `accept()` or `recv()` call, then joins the thread.

**Logging:** Uses `[tcp]` prefix consistent with existing `[sim]` and `[key]` prefixes.

### Shared JSON parser: `sim_ble_parse.c` / `sim_ble_parse.h`

To avoid duplicating `parse_notification_json()` logic from `ble_service.c`, a shared parser is extracted:

```c
// Returns 0 on success, -1 on parse error. Fills out ble_evt_t.
// Returns 1 for set_time (handled inline, no event to enqueue).
int sim_ble_parse_json(const char *buf, uint16_t len, ble_evt_t *out);
```

This is used by `sim_socket.c` for TCP messages. The firmware's `ble_service.c` keeps its own copy (it depends on ESP-IDF APIs like `xQueueSend`, `settimeofday` shimming, etc.), but the logic is identical.

### Notification protocol (same as BLE GATT writes)

```json
{"action": "add", "id": "<session_id>", "project": "<name>", "message": "<text>"}
{"action": "dismiss", "id": "<session_id>"}
{"action": "clear"}
{"action": "set_time", "epoch": 1741825200, "tz": "UTC-3"}
```

`set_time` calls `settimeofday()` and `setenv("TZ", ...)` + `tzset()`, same as the firmware. Both work fine on macOS.

### Config protocol (new, TCP-only)

**Write config** (fire-and-forget, daemon → simulator):
```json
{"action": "write_config", "brightness": 128, "sleep_timeout": 300}
```
Simulator applies via `config_store_set_brightness()`, `config_store_set_sleep_timeout()`, and `ui_manager_set_sleep_timeout()`. Brightness value is stored but has no visual effect in the simulator (no `display_set_brightness()` shim needed — the real display driver is not compiled).

**Read config** (request/response):
- Daemon sends: `{"action": "read_config"}\n`
- Simulator responds: `{"brightness": 128, "sleep_timeout": 300}\n`

The TCP protocol is **half-duplex by convention**: the daemon sends commands, and the simulator only sends data back in response to `read_config`. The simulator never sends unsolicited data. The `read_config` response is handled directly on the socket thread (config reads are atomic — just serialize current config_store state and write back).

### Headless mode interaction

When `--listen` is combined with `--headless`, the simulator runs **indefinitely** (overrides `end_time` calculation) until the process is terminated (SIGTERM/SIGINT). This enables CI and automated testing scenarios where the simulator is driven entirely by the daemon.

### Integration with existing simulator

- Coexists with keyboard input, `--events`, `--scenario` — all event sources feed `ui_manager_handle_event()` (socket events via the queue, others directly from the main thread as before)
- Works in both interactive and headless modes
- The main loop calls `sim_socket_process()` each tick to drain queued events

## Daemon: Simulator Transport

### New module: `sim_client.py`

A `SimClient` class that implements the same interface as `ClawdBleClient`. Both implement a shared `TransportClient` Protocol:

```python
class TransportClient(Protocol):
    is_connected: bool
    async def connect(self) -> None: ...
    async def disconnect(self) -> None: ...
    async def ensure_connected(self) -> None: ...
    async def write_notification(self, payload: str) -> bool: ...
    async def read_config(self) -> dict: ...
    async def write_config(self, payload: str) -> bool: ...
```

Constructor takes `on_disconnect_cb` and `on_connect_cb` callbacks, same as `ClawdBleClient`.

| Method | SimClient Behavior |
|---|---|
| `connect()` | TCP connect to `localhost:port`, retries on connection refused (same retry loop pattern as BLE) |
| `disconnect()` | Close TCP socket |
| `is_connected` | Returns TCP socket connected state |
| `write_notification(payload)` | Sends `payload + "\n"` over TCP, returns `bool` |
| `read_config()` | Sends `{"action": "read_config"}\n`, reads one response line with 2-second timeout, returns parsed `dict` |
| `write_config(payload)` | Sends `payload + "\n"` over TCP, returns `bool` |

Disconnect detection: TCP send failure (broken pipe / `ConnectionResetError`) or recv returning empty bytes (EOF) triggers callbacks, same as BLE.

### Multi-transport architecture

Each transport has its own independent sender coroutine and queue:

- **Per-transport queues:** Each transport gets its own `asyncio.Queue`. When `_handle_message()` receives a message, it puts a copy into every transport's queue.
- **Per-transport sender:** Each transport gets its own `_transport_sender()` coroutine that reads from its own queue. This handles connect/reconnect/sync/replay independently per transport.
- **Independent lifecycle:** BLE can be scanning while TCP is connected and vice versa. One transport failing a write does not affect the other.
- **Observer:** Notified when any transport connects (connected=True) or when all transports disconnect (connected=False).

### Daemon CLI

```
clawd-tank-daemon                    # BLE only (unchanged)
clawd-tank-daemon --sim              # BLE + TCP on default port 19872
clawd-tank-daemon --sim-only         # TCP only (no BLE scanning noise)
clawd-tank-daemon --sim-port 12345   # Custom port (implies --sim)
```

`--sim-only` is useful during development when no ESP32 is available, avoiding noisy BLE scan logs.

## Usage

```bash
# Terminal 1: simulator with TCP listener
./simulator/build/clawd-tank-sim --listen

# Terminal 2: daemon pointing at simulator (no BLE)
clawd-tank-daemon --sim-only

# Terminal 3: send notification (unchanged)
echo '{"event":"add","session_id":"test-1","project":"my-app","message":"Waiting"}' | clawd-tank-notify
```

Or with both transports:
```bash
clawd-tank-daemon --sim   # drives both ESP32 and simulator
```

## File Changes

### New files
- `simulator/sim_socket.c` — TCP listener thread, queue management, event dispatch
- `simulator/sim_socket.h` — Public API (`sim_socket_init`, `sim_socket_process`, `sim_socket_shutdown`)
- `simulator/sim_ble_parse.c` — Shared JSON-to-`ble_evt_t` parser
- `simulator/sim_ble_parse.h` — Parser API
- `host/clawd_tank_daemon/sim_client.py` — TCP transport client
- `host/clawd_tank_daemon/transport.py` — `TransportClient` Protocol definition

### Modified files
- `simulator/sim_main.c` — Add `--listen` CLI option, call `sim_socket_init()` / `sim_socket_process()` / `sim_socket_shutdown()`, handle `--listen` + `--headless` indefinite run
- `simulator/CMakeLists.txt` — Add `sim_socket.c` and `sim_ble_parse.c` to sources
- `host/clawd_tank_daemon/daemon.py` — Multi-transport support, per-transport sender coroutines, `--sim` / `--sim-only` / `--sim-port` flags
- `host/clawd_tank_daemon/ble_client.py` — Implement `TransportClient` Protocol
- `host/clawd_tank_daemon/__main__.py` or CLI entry point — Pass new flags through

## Testing

- **Simulator unit**: Start simulator with `--listen`, send JSON over TCP with a test script, verify events fire
- **Integration**: Run full pipeline (simulator + daemon + clawd-tank-notify), verify notifications appear
- **Simultaneous**: Run with both BLE device and simulator, verify both receive the same notifications
- **Reconnect**: Kill and restart simulator, verify daemon reconnects and replays active notifications
- **Config**: Read/write config over TCP, verify values applied in simulator
- **Headless**: Run `--listen --headless`, verify indefinite running and daemon-driven events
