# Simulator-Daemon Bridge Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enable the daemon to drive the simulator over TCP, so the full Claude Code → daemon → display pipeline works without real ESP32 hardware.

**Architecture:** The simulator gets a TCP socket listener (background pthread + thread-safe queue). The daemon gets a `SimClient` TCP transport alongside `ClawdBleClient`. Both share a `TransportClient` Protocol. The daemon broadcasts to all connected transports independently.

**Tech Stack:** C11 (POSIX sockets, pthreads), Python 3.10+ (asyncio), cJSON

---

## Chunk 1: Simulator-side TCP listener

### Task 1: Create shared JSON parser (`sim_ble_parse`)

**Files:**
- Create: `simulator/sim_ble_parse.h`
- Create: `simulator/sim_ble_parse.c`

- [ ] **Step 1: Create `sim_ble_parse.h`**

```c
// simulator/sim_ble_parse.h
#ifndef SIM_BLE_PARSE_H
#define SIM_BLE_PARSE_H

#include "ble_service.h"
#include <stdint.h>

// Parse a BLE-format JSON payload into a ble_evt_t.
// Returns 0 on success (event written to *out, caller should enqueue).
// Returns 1 for set_time (handled inline, no event to enqueue).
// Returns 2 for config actions (read_config/write_config — caller handles directly).
// Returns -1 on parse error.
int sim_ble_parse_json(const char *buf, uint16_t len, ble_evt_t *out);

#endif
```

- [ ] **Step 2: Create `sim_ble_parse.c`**

Implements `sim_ble_parse_json()` — same logic as `parse_notification_json()` in `firmware/main/ble_service.c:50-117`, but instead of calling `xQueueSend`, it fills the `ble_evt_t *out` struct. For `set_time`, it calls `settimeofday()` and `setenv()`/`tzset()` directly and returns 1.

```c
// simulator/sim_ble_parse.c
#include "sim_ble_parse.h"
#include "cJSON.h"
#include "config_store.h"
#include "ui_manager.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <stdlib.h>

static void safe_strncpy(char *dst, const char *src, size_t n) {
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, n - 1);
    dst[n - 1] = '\0';
}

int sim_ble_parse_json(const char *buf, uint16_t len, ble_evt_t *out) {
    cJSON *json = cJSON_ParseWithLength(buf, len);
    if (!json) return -1;

    cJSON *action = cJSON_GetObjectItem(json, "action");
    if (!action || !cJSON_IsString(action)) {
        cJSON_Delete(json);
        return -1;
    }

    memset(out, 0, sizeof(*out));

    if (strcmp(action->valuestring, "add") == 0) {
        out->type = BLE_EVT_NOTIF_ADD;
        cJSON *id = cJSON_GetObjectItem(json, "id");
        if (!id || !cJSON_IsString(id)) { cJSON_Delete(json); return -1; }
        safe_strncpy(out->id, id->valuestring, sizeof(out->id));
        cJSON *project = cJSON_GetObjectItem(json, "project");
        cJSON *message = cJSON_GetObjectItem(json, "message");
        safe_strncpy(out->project,
                     project && cJSON_IsString(project) ? project->valuestring : "",
                     sizeof(out->project));
        safe_strncpy(out->message,
                     message && cJSON_IsString(message) ? message->valuestring : "",
                     sizeof(out->message));
    } else if (strcmp(action->valuestring, "dismiss") == 0) {
        out->type = BLE_EVT_NOTIF_DISMISS;
        cJSON *id = cJSON_GetObjectItem(json, "id");
        if (!id || !cJSON_IsString(id)) { cJSON_Delete(json); return -1; }
        safe_strncpy(out->id, id->valuestring, sizeof(out->id));
    } else if (strcmp(action->valuestring, "clear") == 0) {
        out->type = BLE_EVT_NOTIF_CLEAR;
    } else if (strcmp(action->valuestring, "set_time") == 0) {
        cJSON *epoch = cJSON_GetObjectItem(json, "epoch");
        if (epoch && cJSON_IsNumber(epoch)) {
            struct timeval tv = { .tv_sec = (time_t)epoch->valuedouble, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            printf("[tcp] System time set to epoch %lld\n", (long long)tv.tv_sec);
        }
        cJSON *tz = cJSON_GetObjectItem(json, "tz");
        if (tz && cJSON_IsString(tz)) {
            setenv("TZ", tz->valuestring, 1);
            tzset();
            printf("[tcp] Timezone set to %s\n", tz->valuestring);
        }
        cJSON_Delete(json);
        return 1;
    } else if (strcmp(action->valuestring, "write_config") == 0 ||
               strcmp(action->valuestring, "read_config") == 0) {
        /* Config actions are not BLE events — return 2 to signal
         * the caller to handle them directly (on the socket thread
         * for read_config, or enqueued for write_config). */
        cJSON_Delete(json);
        return 2;
    } else {
        cJSON_Delete(json);
        return -1;
    }

    cJSON_Delete(json);
    return 0;
}
```

- [ ] **Step 3: Add `sim_ble_parse.c` to `simulator/CMakeLists.txt`**

In `CMakeLists.txt`, add `sim_ble_parse.c` to the `add_executable` source list, after `sim_events.c`:

```cmake
    sim_ble_parse.c
```

- [ ] **Step 4: Build simulator to verify compilation**

Run: `cd /Users/marciorodrigues/Projects/clawd-tank/simulator && cmake -B build && cmake --build build`
Expected: Compiles with no errors.

- [ ] **Step 5: Commit**

```bash
git add simulator/sim_ble_parse.h simulator/sim_ble_parse.c simulator/CMakeLists.txt
git commit -m "feat(sim): add shared BLE JSON parser for TCP bridge"
```

---

### Task 2: Create TCP socket listener (`sim_socket`)

**Files:**
- Create: `simulator/sim_socket.h`
- Create: `simulator/sim_socket.c`
- Modify: `simulator/CMakeLists.txt`

- [ ] **Step 1: Create `sim_socket.h`**

```c
// simulator/sim_socket.h
#ifndef SIM_SOCKET_H
#define SIM_SOCKET_H

#include <stdbool.h>

#define SIM_SOCKET_DEFAULT_PORT 19872

// Start TCP listener on given port. Spawns a background thread.
// Returns 0 on success, -1 on error.
int sim_socket_init(int port);

// Drain any queued events from the socket thread.
// Call from the main loop before ui_manager_tick().
// Returns true if any event was processed.
bool sim_socket_process(void);

// Stop listener, close sockets, join thread.
void sim_socket_shutdown(void);

#endif
```

- [ ] **Step 2: Create `sim_socket.c`**

This file contains:
1. A mutex-guarded ring buffer for `ble_evt_t` (capacity 16)
2. A background pthread that binds/listens/accepts/reads
3. `sim_socket_process()` that drains the queue from the main thread

```c
// simulator/sim_socket.c
#include "sim_socket.h"
#include "sim_ble_parse.h"
#include "ble_service.h"
#include "config_store.h"
#include "ui_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

/* ---- Thread-safe event queue ---- */
#define EVT_QUEUE_SIZE 16

static ble_evt_t s_queue[EVT_QUEUE_SIZE];
static int s_queue_head = 0;
static int s_queue_tail = 0;
static int s_queue_count = 0;
static pthread_mutex_t s_queue_mutex = PTHREAD_MUTEX_INITIALIZER;

static bool queue_push(const ble_evt_t *evt) {
    pthread_mutex_lock(&s_queue_mutex);
    if (s_queue_count >= EVT_QUEUE_SIZE) {
        pthread_mutex_unlock(&s_queue_mutex);
        printf("[tcp] Event queue full, dropping event\n");
        return false;
    }
    s_queue[s_queue_tail] = *evt;
    s_queue_tail = (s_queue_tail + 1) % EVT_QUEUE_SIZE;
    s_queue_count++;
    pthread_mutex_unlock(&s_queue_mutex);
    return true;
}

static bool queue_pop(ble_evt_t *out) {
    pthread_mutex_lock(&s_queue_mutex);
    if (s_queue_count == 0) {
        pthread_mutex_unlock(&s_queue_mutex);
        return false;
    }
    *out = s_queue[s_queue_head];
    s_queue_head = (s_queue_head + 1) % EVT_QUEUE_SIZE;
    s_queue_count--;
    pthread_mutex_unlock(&s_queue_mutex);
    return true;
}

/* ---- Pending config update (socket thread → main thread) ---- */
static volatile bool s_pending_config_update = false;
static volatile uint32_t s_pending_sleep_timeout_ms = 0;

/* ---- TCP listener thread ---- */
static int s_listen_fd = -1;
static pthread_t s_thread;
static volatile bool s_running = false;

/* Handle config read/write actions on the socket thread.
 * read_config: atomic read of config_store state (no LVGL interaction).
 * write_config: stores values in config_store (atomic word-sized writes)
 *   and enqueues a dummy event so the main thread can call
 *   ui_manager_set_sleep_timeout(). */
static void handle_config_action(const char *buf, uint16_t len, int client_fd) {
    cJSON *json = cJSON_ParseWithLength(buf, len);
    if (!json) return;

    cJSON *action = cJSON_GetObjectItem(json, "action");
    if (!action || !cJSON_IsString(action)) { cJSON_Delete(json); return; }

    if (strcmp(action->valuestring, "read_config") == 0) {
        char config_buf[128];
        uint16_t config_len = config_store_serialize_json(config_buf, sizeof(config_buf));
        if (config_len > 0) {
            send(client_fd, config_buf, config_len, 0);
            send(client_fd, "\n", 1, 0);
            printf("[tcp] Config read: %s\n", config_buf);
        }
    } else if (strcmp(action->valuestring, "write_config") == 0) {
        cJSON *brightness = cJSON_GetObjectItem(json, "brightness");
        if (brightness && cJSON_IsNumber(brightness)) {
            config_store_set_brightness((uint8_t)brightness->valueint);
            printf("[tcp] Config: brightness=%d\n", brightness->valueint);
        }
        cJSON *sleep_t = cJSON_GetObjectItem(json, "sleep_timeout");
        if (sleep_t && cJSON_IsNumber(sleep_t)) {
            config_store_set_sleep_timeout((uint16_t)sleep_t->valueint);
            printf("[tcp] Config: sleep_timeout=%d\n", sleep_t->valueint);
        }
        /* Signal the main thread to apply sleep timeout via
         * ui_manager_set_sleep_timeout (must be called from main thread
         * since it writes to LVGL-adjacent state). The main loop's
         * sim_socket_process() checks this flag each tick. */
        if (sleep_t && cJSON_IsNumber(sleep_t)) {
            s_pending_sleep_timeout_ms = (uint32_t)sleep_t->valueint * 1000;
            s_pending_config_update = true;
        }
    }

    cJSON_Delete(json);
}

static void handle_client(int client_fd) {
    printf("[tcp] Client connected\n");

    ble_evt_t connect_evt = { .type = BLE_EVT_CONNECTED };
    queue_push(&connect_evt);

    char buf[4096];
    int buf_len = 0;

    while (s_running) {
        int n = (int)recv(client_fd, buf + buf_len, sizeof(buf) - buf_len - 1, 0);
        if (n <= 0) break;  /* EOF or error */
        buf_len += n;
        buf[buf_len] = '\0';

        /* Process complete lines */
        char *line_start = buf;
        char *newline;
        while ((newline = strchr(line_start, '\n')) != NULL) {
            *newline = '\0';
            int line_len = (int)(newline - line_start);
            if (line_len > 0) {
                ble_evt_t evt;
                int rc = sim_ble_parse_json(line_start, (uint16_t)line_len, &evt);
                if (rc == 0) {
                    queue_push(&evt);
                } else if (rc == 2) {
                    /* Config action — handle on socket thread */
                    handle_config_action(line_start, (uint16_t)line_len, client_fd);
                } else if (rc < 0) {
                    printf("[tcp] Parse error, ignoring: %.*s\n", line_len, line_start);
                }
                /* rc == 1 means set_time handled inline by parser */
            }
            line_start = newline + 1;
        }

        /* Shift remaining partial line to start of buffer */
        int remaining = buf_len - (int)(line_start - buf);
        if (remaining > 0 && line_start != buf) {
            memmove(buf, line_start, remaining);
        }
        buf_len = remaining;
    }

    printf("[tcp] Client disconnected\n");
    ble_evt_t disconnect_evt = { .type = BLE_EVT_DISCONNECTED };
    queue_push(&disconnect_evt);

    close(client_fd);
}

static void *listener_thread(void *arg) {
    (void)arg;
    printf("[tcp] Listener thread started\n");

    while (s_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(s_listen_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (s_running) {
                printf("[tcp] Accept error: %s\n", strerror(errno));
            }
            continue;
        }
        handle_client(client_fd);
    }

    printf("[tcp] Listener thread exiting\n");
    return NULL;
}

/* ---- Public API ---- */

int sim_socket_init(int port) {
    s_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_listen_fd < 0) {
        fprintf(stderr, "[tcp] Failed to create socket: %s\n", strerror(errno));
        return -1;
    }

    int opt = 1;
    setsockopt(s_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons((uint16_t)port),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if (bind(s_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[tcp] Failed to bind port %d: %s\n", port, strerror(errno));
        close(s_listen_fd);
        s_listen_fd = -1;
        return -1;
    }

    if (listen(s_listen_fd, 1) < 0) {
        fprintf(stderr, "[tcp] Failed to listen: %s\n", strerror(errno));
        close(s_listen_fd);
        s_listen_fd = -1;
        return -1;
    }

    printf("[tcp] Listening on port %d\n", port);
    s_running = true;

    if (pthread_create(&s_thread, NULL, listener_thread, NULL) != 0) {
        fprintf(stderr, "[tcp] Failed to create listener thread\n");
        close(s_listen_fd);
        s_listen_fd = -1;
        s_running = false;
        return -1;
    }

    return 0;
}

bool sim_socket_process(void) {
    bool any = false;
    ble_evt_t evt;
    while (queue_pop(&evt)) {
        ui_manager_handle_event(&evt);
        any = true;
    }
    /* Apply pending config updates from socket thread */
    if (s_pending_config_update) {
        s_pending_config_update = false;
        ui_manager_set_sleep_timeout(s_pending_sleep_timeout_ms);
    }
    return any;
}

void sim_socket_shutdown(void) {
    s_running = false;
    if (s_listen_fd >= 0) {
        shutdown(s_listen_fd, SHUT_RDWR);
        close(s_listen_fd);
        s_listen_fd = -1;
    }
    pthread_join(s_thread, NULL);
    printf("[tcp] Shut down\n");
}
```

- [ ] **Step 3: Add `sim_socket.c` to `simulator/CMakeLists.txt`**

Add `sim_socket.c` to the `add_executable` source list after `sim_ble_parse.c`.

- [ ] **Step 4: Build simulator to verify compilation**

Run: `cd /Users/marciorodrigues/Projects/clawd-tank/simulator && cmake -B build && cmake --build build`
Expected: Compiles with no errors.

- [ ] **Step 5: Commit**

```bash
git add simulator/sim_socket.h simulator/sim_socket.c simulator/CMakeLists.txt
git commit -m "feat(sim): add TCP socket listener with thread-safe queue"
```

---

### Task 3: Integrate TCP listener into simulator main

**Files:**
- Modify: `simulator/sim_main.c:14-74` (CLI options and parse_args)
- Modify: `simulator/sim_main.c:103-136` (run_headless)
- Modify: `simulator/sim_main.c:143-161` (run_interactive)
- Modify: `simulator/sim_main.c:254-285` (main)

- [ ] **Step 1: Add `--listen` CLI option parsing**

In `sim_main.c`, add to the CLI options section (after line 21):
```c
static int      opt_listen_port = 0;  /* 0 = disabled */
```

In `print_usage()`, add to the Events section:
```c
        "  --listen [port]         Listen for daemon TCP connections (default: 19872)\n"
```

In `parse_args()`, add a case for `--listen`:
```c
        } else if (strcmp(argv[i], "--listen") == 0) {
            /* Port is optional — check if next arg is a number */
            if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') {
                opt_listen_port = atoi(argv[++i]);
            } else {
                opt_listen_port = SIM_SOCKET_DEFAULT_PORT;
            }
```

Add includes at top:
```c
#include "sim_socket.h"
#include <unistd.h>  /* for usleep in headless+listen mode */
```

- [ ] **Step 2: Add `sim_socket_process()` to interactive loop**

In `run_interactive()`, add `sim_socket_process()` call after `handle_sdl_events()` and before `ui_manager_tick()`:
```c
        /* Process TCP socket events */
        if (opt_listen_port > 0) {
            sim_socket_process();
        }
```

- [ ] **Step 3: Modify headless loop for `--listen` indefinite mode**

In `run_headless()`, when `--listen` is active, run indefinitely instead of using a fixed end time. Replace the `run_headless` function body to handle this:

After the existing `end_time` calculation, add:
```c
    bool indefinite = (opt_listen_port > 0);
    if (indefinite) {
        printf("[sim] Headless + listen mode: running indefinitely (Ctrl-C to stop)\n");
    }
```

Change the `while (time < end_time)` loop condition to:
```c
    while (indefinite || time < end_time) {
```

Add `sim_socket_process()` call inside the loop, after `sim_events_process`:
```c
        /* Process TCP socket events */
        if (opt_listen_port > 0) {
            sim_socket_process();
        }
```

In headless indefinite mode, use real `usleep(TICK_MS * 1000)` instead of only advancing simulated time (so the simulator actually waits between ticks and can receive TCP data). Add before `sim_advance_tick`:
```c
        if (indefinite) {
            usleep(TICK_MS * 1000);
        }
```

- [ ] **Step 4: Initialize and shut down socket in main()**

In `main()`, after event init (step 3), add:
```c
    /* 3b. Init TCP listener */
    if (opt_listen_port > 0) {
        if (sim_socket_init(opt_listen_port) != 0) {
            fprintf(stderr, "Failed to start TCP listener on port %d\n", opt_listen_port);
            return 1;
        }
    }
```

Before `sim_display_shutdown()` in cleanup, add:
```c
    /* 7a. Shutdown TCP listener */
    if (opt_listen_port > 0) {
        sim_socket_shutdown();
    }
```

- [ ] **Step 5: Build and test manually**

Run: `cd /Users/marciorodrigues/Projects/clawd-tank/simulator && cmake -B build && cmake --build build`
Expected: Compiles with no errors.

Quick smoke test — start simulator, send JSON via netcat:
```bash
# Terminal 1:
./simulator/build/clawd-tank-sim --listen

# Terminal 2:
echo '{"action":"add","id":"test-1","project":"my-app","message":"Hello"}' | nc localhost 19872
```
Expected: Simulator shows "Connected", then the notification card appears.

- [ ] **Step 6: Commit**

```bash
git add simulator/sim_main.c
git commit -m "feat(sim): integrate TCP listener into simulator main loop"
```

---

## Chunk 2: Daemon-side TCP transport

### Task 4: Create `TransportClient` Protocol

**Files:**
- Create: `host/clawd_tank_daemon/transport.py`

- [ ] **Step 1: Create `transport.py`**

```python
"""Transport client protocol for Clawd Tank daemon."""

from typing import Protocol, runtime_checkable


@runtime_checkable
class TransportClient(Protocol):
    """Interface shared by BLE and simulator TCP transports."""

    @property
    def is_connected(self) -> bool: ...

    async def connect(self) -> None: ...

    async def disconnect(self) -> None: ...

    async def ensure_connected(self) -> None: ...

    async def write_notification(self, payload: str) -> bool: ...

    async def read_config(self) -> dict: ...

    async def write_config(self, payload: str) -> bool: ...
```

- [ ] **Step 2: Commit**

```bash
git add host/clawd_tank_daemon/transport.py
git commit -m "feat(daemon): add TransportClient protocol"
```

---

### Task 5: Create `SimClient` TCP transport

**Files:**
- Create: `host/clawd_tank_daemon/sim_client.py`
- Create: `host/tests/test_sim_client.py`

- [ ] **Step 1: Write failing tests for `SimClient`**

```python
# host/tests/test_sim_client.py
"""Tests for SimClient TCP transport."""

import asyncio
import json
import pytest
from clawd_tank_daemon.sim_client import SimClient


async def start_mock_server(handler):
    """Start a TCP server on an OS-assigned port. Returns (server, port)."""
    server = await asyncio.start_server(handler, "127.0.0.1", 0)
    port = server.sockets[0].getsockname()[1]
    return server, port


@pytest.mark.asyncio
async def test_connect_and_write():
    received = []

    async def handler(reader, writer):
        while True:
            line = await reader.readline()
            if not line:
                break
            received.append(line.decode().strip())
        writer.close()

    server, port = await start_mock_server(handler)
    async with server:
        client = SimClient(port=port)
        await client.connect()
        assert client.is_connected

        payload = json.dumps({"action": "add", "id": "s1", "project": "p", "message": "m"})
        result = await client.write_notification(payload)
        assert result is True

        await client.disconnect()
        assert not client.is_connected
        await asyncio.sleep(0.05)

    assert len(received) == 1
    assert json.loads(received[0])["action"] == "add"


@pytest.mark.asyncio
async def test_read_config():
    async def handler(reader, writer):
        line = await reader.readline()
        req = json.loads(line.decode().strip())
        if req.get("action") == "read_config":
            writer.write(b'{"brightness":128,"sleep_timeout":300}\n')
            await writer.drain()
        writer.close()

    server, port = await start_mock_server(handler)
    async with server:
        client = SimClient(port=port)
        await client.connect()

        config = await client.read_config()
        assert config == {"brightness": 128, "sleep_timeout": 300}

        await client.disconnect()


@pytest.mark.asyncio
async def test_write_config():
    received = []

    async def handler(reader, writer):
        line = await reader.readline()
        if line:
            received.append(line.decode().strip())
        writer.close()

    server, port = await start_mock_server(handler)
    async with server:
        client = SimClient(port=port)
        await client.connect()

        result = await client.write_config(
            json.dumps({"action": "write_config", "brightness": 200})
        )
        assert result is True

        await client.disconnect()
        await asyncio.sleep(0.05)

    assert len(received) == 1
    assert json.loads(received[0])["brightness"] == 200


@pytest.mark.asyncio
async def test_write_after_server_close_returns_false():
    """After server closes connection, write should fail and mark disconnected."""
    async def handler(reader, writer):
        writer.close()
        await writer.wait_closed()

    server, port = await start_mock_server(handler)
    async with server:
        client = SimClient(port=port)
        await client.connect()
        await asyncio.sleep(0.1)  # Let server close propagate
        result = await client.write_notification('{"action":"clear"}')
        # Either write fails or next write will fail
        if result:
            await asyncio.sleep(0.05)
            result = await client.write_notification('{"action":"clear"}')
        assert result is False
        assert not client.is_connected


@pytest.mark.asyncio
async def test_connect_retries_on_refused():
    """connect() should retry when connection is refused."""
    # Find a port that is guaranteed free
    temp_server, port = await start_mock_server(lambda r, w: w.close())
    temp_server.close()
    await temp_server.wait_closed()

    client = SimClient(port=port, retry_interval=0.05)

    # Start server after a delay
    async def delayed_server():
        await asyncio.sleep(0.15)
        return await asyncio.start_server(
            lambda r, w: None,
            "127.0.0.1", port,
        )

    server_task = asyncio.create_task(delayed_server())
    connect_task = asyncio.create_task(client.connect())

    server = await server_task
    async with server:
        await asyncio.wait_for(connect_task, timeout=2.0)
        assert client.is_connected
        await client.disconnect()


@pytest.mark.asyncio
async def test_write_when_disconnected_returns_false():
    client = SimClient(port=1)  # Port doesn't matter, never connects
    result = await client.write_notification('{"action":"clear"}')
    assert result is False
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd /Users/marciorodrigues/Projects/clawd-tank/host && .venv/bin/pytest tests/test_sim_client.py -v`
Expected: FAIL with `ModuleNotFoundError: No module named 'clawd_tank_daemon.sim_client'`

- [ ] **Step 3: Implement `SimClient`**

```python
# host/clawd_tank_daemon/sim_client.py
"""TCP transport client for connecting to the Clawd Tank simulator."""

import asyncio
import json
import logging

logger = logging.getLogger("clawd-tank.sim")

SIM_DEFAULT_PORT = 19872
SIM_RETRY_INTERVAL = 5  # seconds


class SimClient:
    """TCP client that connects to the simulator's TCP listener."""

    def __init__(
        self,
        host: str = "127.0.0.1",
        port: int = SIM_DEFAULT_PORT,
        on_disconnect_cb=None,
        on_connect_cb=None,
        retry_interval: float = SIM_RETRY_INTERVAL,
    ):
        self._host = host
        self._port = port
        self._reader: asyncio.StreamReader | None = None
        self._writer: asyncio.StreamWriter | None = None
        self._on_disconnect_cb = on_disconnect_cb
        self._on_connect_cb = on_connect_cb
        self._retry_interval = retry_interval

    @property
    def is_connected(self) -> bool:
        return self._writer is not None and not self._writer.is_closing()

    async def connect(self) -> None:
        """Connect to the simulator. Retries until successful."""
        while True:
            try:
                logger.info("Connecting to simulator at %s:%d...", self._host, self._port)
                self._reader, self._writer = await asyncio.open_connection(
                    self._host, self._port
                )
                logger.info("Connected to simulator")
                if self._on_connect_cb:
                    self._on_connect_cb()
                return
            except (ConnectionRefusedError, OSError) as e:
                logger.debug("Simulator not available: %s, retrying...", e)
                await asyncio.sleep(self._retry_interval)

    async def disconnect(self) -> None:
        """Close the TCP connection."""
        if self._writer:
            try:
                self._writer.close()
                await self._writer.wait_closed()
            except Exception:
                pass
        self._writer = None
        self._reader = None

    async def ensure_connected(self) -> None:
        """Reconnect if disconnected."""
        if not self.is_connected:
            await self.connect()

    async def write_notification(self, payload: str) -> bool:
        """Send a JSON payload followed by newline. Returns True on success."""
        if not self.is_connected:
            logger.warning("Not connected to simulator, cannot write")
            return False
        try:
            self._writer.write((payload + "\n").encode("utf-8"))
            await self._writer.drain()
            return True
        except (ConnectionResetError, BrokenPipeError, OSError) as e:
            logger.error("Simulator write failed: %s", e)
            self._handle_disconnect()
            return False

    async def read_config(self) -> dict:
        """Request and read config from simulator. Returns empty dict on error."""
        if not self.is_connected:
            return {}
        try:
            self._writer.write(b'{"action":"read_config"}\n')
            await self._writer.drain()
            line = await asyncio.wait_for(self._reader.readline(), timeout=2.0)
            if not line:
                self._handle_disconnect()
                return {}
            return json.loads(line.decode("utf-8").strip())
        except (asyncio.TimeoutError, json.JSONDecodeError, OSError) as e:
            logger.error("Config read failed: %s", e)
            return {}

    async def write_config(self, payload: str) -> bool:
        """Send a config write payload. Returns True on success."""
        return await self.write_notification(payload)

    def _handle_disconnect(self) -> None:
        """Clean up state and notify on disconnect."""
        self._writer = None
        self._reader = None
        if self._on_disconnect_cb:
            self._on_disconnect_cb()
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd /Users/marciorodrigues/Projects/clawd-tank/host && .venv/bin/pytest tests/test_sim_client.py -v`
Expected: All tests PASS.

- [ ] **Step 5: Commit**

```bash
git add host/clawd_tank_daemon/sim_client.py host/tests/test_sim_client.py
git commit -m "feat(daemon): add SimClient TCP transport with tests"
```

---

### Task 6: Refactor daemon for multi-transport support

**Files:**
- Modify: `host/clawd_tank_daemon/daemon.py`
- Modify: `host/tests/test_daemon.py`

- [ ] **Step 1: Write failing test for multi-transport broadcast**

Add to `host/tests/test_daemon.py`:

```python
@pytest.mark.asyncio
async def test_handle_message_broadcasts_to_all_transport_queues():
    """When sim is enabled, messages go to all transport queues."""
    daemon = ClawdDaemon(sim_port=19872)
    msg = {"event": "add", "session_id": "s1", "project": "p", "message": "m"}
    await daemon._handle_message(msg)
    # Each transport queue should have the message
    for q in daemon._transport_queues.values():
        assert q.qsize() == 1


@pytest.mark.asyncio
async def test_sim_only_mode_has_no_ble_transport():
    """In sim-only mode, only the sim transport exists."""
    daemon = ClawdDaemon(sim_port=19872, sim_only=True)
    assert "ble" not in daemon._transports
    assert "sim" in daemon._transports
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd /Users/marciorodrigues/Projects/clawd-tank/host && .venv/bin/pytest tests/test_daemon.py::test_handle_message_broadcasts_to_all_transport_queues tests/test_daemon.py::test_sim_only_mode_has_no_ble_transport -v`
Expected: FAIL (ClawdDaemon doesn't accept `sim_port` / `sim_only` params yet).

- [ ] **Step 3: Refactor `ClawdDaemon` for multi-transport**

In `host/clawd_tank_daemon/daemon.py`, make these changes:

1. Add imports:
```python
from .sim_client import SimClient
from .transport import TransportClient
```

2. Update `ClawdDaemon.__init__` to accept `sim_port` and `sim_only` params:
```python
def __init__(self, observer=None, headless=True, sim_port=0, sim_only=False):
```

3. Replace single `_ble` client with `_transports` dict and `_transport_queues` dict:
```python
    self._transports: dict[str, TransportClient] = {}
    self._transport_queues: dict[str, asyncio.Queue] = {}

    if not sim_only:
        ble = ClawdBleClient(
            on_disconnect_cb=lambda: self._on_transport_disconnect("ble"),
            on_connect_cb=lambda: self._on_transport_connect("ble"),
        )
        self._transports["ble"] = ble
        self._transport_queues["ble"] = asyncio.Queue()

    if sim_port > 0:
        sim = SimClient(
            port=sim_port,
            on_disconnect_cb=lambda: self._on_transport_disconnect("sim"),
            on_connect_cb=lambda: self._on_transport_connect("sim"),
        )
        self._transports["sim"] = sim
        self._transport_queues["sim"] = asyncio.Queue()

    self._active_notifications: dict[str, dict] = {}
    self._running = True
    self._shutdown_event = asyncio.Event()
    self._lock_fd: int | None = None
    self._observer = observer
    self._headless = headless
```

4. Update `_handle_message` to broadcast to all transport queues:
```python
    async def _handle_message(self, msg: dict) -> None:
        event = msg.get("event")
        session_id = msg.get("session_id", "")

        if event == "add":
            self._active_notifications[session_id] = msg
        elif event == "dismiss":
            self._active_notifications.pop(session_id, None)

        for q in self._transport_queues.values():
            await q.put(msg)

        if self._observer:
            self._observer.on_notification_change(len(self._active_notifications))
```

5. Replace `_on_ble_connect`/`_on_ble_disconnect` with transport-aware versions:
```python
    def _on_transport_connect(self, name: str) -> None:
        logger.info("Transport '%s' connected", name)
        if self._observer:
            self._observer.on_connection_change(True)

    def _on_transport_disconnect(self, name: str) -> None:
        logger.warning("Transport '%s' disconnected", name)
        if self._observer:
            any_connected = any(t.is_connected for t in self._transports.values())
            self._observer.on_connection_change(any_connected)
```

6. Replace `_ble_sender` with a generic `_transport_sender` that takes a name:
```python
    async def _transport_sender(self, name: str) -> None:
        transport = self._transports[name]
        queue = self._transport_queues[name]
        while self._running:
            try:
                msg = await asyncio.wait_for(queue.get(), timeout=1.0)
            except asyncio.TimeoutError:
                continue
            try:
                payload = daemon_message_to_ble_payload(msg)
            except ValueError:
                logger.error("[%s] Skipping unknown event: %s", name, msg.get("event"))
                continue

            was_connected = transport.is_connected
            await transport.ensure_connected()
            if not was_connected and transport.is_connected:
                await self._sync_time_for(transport)
                if self._observer:
                    self._observer.on_connection_change(True)

            success = await transport.write_notification(payload)

            if not success:
                was_connected = transport.is_connected
                await transport.ensure_connected()
                if not was_connected and transport.is_connected:
                    await self._sync_time_for(transport)
                    if self._observer:
                        self._observer.on_connection_change(True)
                await self._replay_active_for(transport)
```

7. Add transport-specific `_sync_time_for` and `_replay_active_for`:
```python
    async def _sync_time_for(self, transport) -> None:
        epoch = int(time.time())
        utc_offset = time.localtime().tm_gmtoff
        sign = "-" if utc_offset >= 0 else "+"
        abs_offset = abs(utc_offset)
        hours, remainder = divmod(abs_offset, 3600)
        minutes = remainder // 60
        tz = f"UTC{sign}{hours}" if minutes == 0 else f"UTC{sign}{hours}:{minutes:02d}"
        payload = json.dumps({"action": "set_time", "epoch": epoch, "tz": tz})
        await transport.write_notification(payload)
        logger.info("Synced time: epoch %d, tz %s", epoch, tz)

    async def _replay_active_for(self, transport) -> None:
        logger.info("Replaying %d active notifications", len(self._active_notifications))
        for msg in list(self._active_notifications.values()):
            try:
                payload = daemon_message_to_ble_payload(msg)
            except ValueError:
                continue
            await transport.write_notification(payload)
            await asyncio.sleep(0.05)
```

8. Update `_shutdown` to disconnect all transports:
```python
    async def _shutdown(self) -> None:
        logger.info("Shutting down...")
        self._running = False
        self._shutdown_event.set()

        clear_payload = daemon_message_to_ble_payload({"event": "clear"})
        for transport in self._transports.values():
            if transport.is_connected:
                await transport.write_notification(clear_payload)
            await transport.disconnect()
        await self._socket.stop()
        self._remove_pid()
        if self._lock_fd is not None:
            os.close(self._lock_fd)
            self._lock_fd = None
```

9. Update `run()` to spawn a sender per transport:
```python
    async def run(self) -> None:
        logging.basicConfig(
            level=logging.INFO,
            format="%(asctime)s [%(name)s] %(levelname)s: %(message)s",
        )

        self._lock_fd = _acquire_lock(takeover=not self._headless)
        self._write_pid()

        if self._headless:
            loop = asyncio.get_running_loop()
            for sig in (signal.SIGTERM, signal.SIGINT):
                loop.add_signal_handler(sig, lambda: asyncio.create_task(self._shutdown()))

        await self._socket.start()

        tasks = []
        for name in self._transports:
            tasks.append(asyncio.create_task(self._transports[name].connect()))
            tasks.append(asyncio.create_task(self._transport_sender(name)))

        await self._shutdown_event.wait()

        for task in tasks:
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass
```

10. Add public methods so the menubar app doesn't need to access `_transports` directly. These replace the old `daemon._ble.read_config()` / `write_config()` / `ensure_connected()` pattern:
```python
    async def read_config(self) -> dict:
        """Read config from the first connected transport."""
        for transport in self._transports.values():
            if transport.is_connected:
                return await transport.read_config()
        return {}

    async def write_config(self, payload: str) -> bool:
        """Write config to all connected transports."""
        success = False
        for transport in self._transports.values():
            if transport.is_connected:
                if await transport.write_config(payload):
                    success = True
        return success

    async def reconnect(self) -> None:
        """Force reconnect on all transports."""
        for transport in self._transports.values():
            await transport.ensure_connected()
```

11. Update `main()` to accept CLI args:
```python
def main():
    import argparse
    parser = argparse.ArgumentParser(description="Clawd Tank daemon")
    parser.add_argument("--sim", action="store_true", help="Enable simulator transport (BLE + TCP)")
    parser.add_argument("--sim-only", action="store_true", help="Simulator only (no BLE)")
    parser.add_argument("--sim-port", type=int, default=SIM_DEFAULT_PORT,
                        help=f"Simulator TCP port (default: {SIM_DEFAULT_PORT})")
    args = parser.parse_args()

    sim_port = 0
    if args.sim or args.sim_only or args.sim_port != SIM_DEFAULT_PORT:
        sim_port = args.sim_port

    daemon = ClawdDaemon(sim_port=sim_port, sim_only=args.sim_only)
    asyncio.run(daemon.run())
```

Add import at top:
```python
from .sim_client import SIM_DEFAULT_PORT
```

- [ ] **Step 4: Update existing tests in `test_daemon.py`, `test_observer.py`, and `test_menubar.py`**

Apply these migration patterns across all three test files:

**Pattern A: Queue size checks** — replace `daemon._pending_queue.qsize()` with:
```python
daemon._transport_queues["ble"].qsize()
```

**Pattern B: Mock BLE transport** — replace `daemon._ble = AsyncMock()` with:
```python
daemon._transports["ble"] = AsyncMock()
daemon._transports["ble"].is_connected = True
```

**Pattern C: BLE sender** — replace `daemon._ble_sender()` with:
```python
daemon._transport_sender("ble")
```

**Pattern D: Disconnect callback** — replace `daemon._on_ble_disconnect()` with:
```python
daemon._on_transport_disconnect("ble")
```

**Pattern E: Queue put for sender tests** — replace `daemon._pending_queue.put(msg)` with:
```python
daemon._transport_queues["ble"].put(msg)
```

**Specific files:**

In `test_daemon.py`: apply patterns A, B, C, E to all tests that reference `_ble`, `_pending_queue`, `_ble_sender`.

In `test_observer.py`:
- `test_observer_connection_via_disconnect_callback` (line 69): apply pattern D
- `test_observer_connection_true_on_ble_sender_connect` (lines 78-100): apply patterns B, E, C

In `test_menubar.py`:
- `test_disconnect_callback_fires_observer` (line 48): apply pattern D

- [ ] **Step 5: Run all affected tests**

Run: `cd /Users/marciorodrigues/Projects/clawd-tank/host && .venv/bin/pytest tests/test_daemon.py tests/test_observer.py tests/test_menubar.py -v`
Expected: All tests PASS.

- [ ] **Step 6: Run full test suite**

Run: `cd /Users/marciorodrigues/Projects/clawd-tank/host && .venv/bin/pytest -v`
Expected: All tests PASS (including test_protocol.py, test_socket_server.py, etc.).

- [ ] **Step 7: Commit**

```bash
git add host/clawd_tank_daemon/daemon.py host/tests/test_daemon.py host/tests/test_observer.py host/tests/test_menubar.py
git commit -m "feat(daemon): multi-transport support with per-transport queues and senders"
```

---

## Chunk 3: Integration and menubar app update

### Task 7: Update menubar app to use daemon public API

**Files:**
- Modify: `host/clawd_tank_menubar/app.py:134,199,212,225` (replace `daemon._ble.*` calls)

- [ ] **Step 1: Replace direct `_ble` access with public daemon methods**

In `host/clawd_tank_menubar/app.py`, make these replacements:

Line 134: `config = await self._daemon._ble.read_config()` →
```python
config = await self._daemon.read_config()
```

Line 199: `self._daemon._ble.write_config(payload)` →
```python
self._daemon.write_config(payload)
```

Line 212: `self._daemon._ble.write_config(payload)` →
```python
self._daemon.write_config(payload)
```

Line 225: `self._daemon._ble.ensure_connected()` →
```python
self._daemon.reconnect()
```

- [ ] **Step 2: Run menubar tests**

Run: `cd /Users/marciorodrigues/Projects/clawd-tank/host && .venv/bin/pytest tests/test_menubar.py -v`
Expected: All tests PASS.

- [ ] **Step 3: Commit**

```bash
git add host/clawd_tank_menubar/app.py
git commit -m "refactor(menubar): use daemon public API instead of accessing _ble directly"
```

---

### Task 8: Update `clawd-tank-notify` daemon auto-start for `--sim` flag

**Files:**
- Modify: `host/clawd-tank-notify:46` (daemon start command)

- [ ] **Step 1: Review current daemon start command**

The notify script starts the daemon with:
```python
[sys.executable, "-m", "clawd_tank_daemon.daemon"]
```

This is fine — it starts BLE-only by default. Users who want sim mode start the daemon manually with `--sim` or `--sim-only`. No changes needed to `clawd-tank-notify`.

- [ ] **Step 2: Verify `clawd-tank-notify` still works**

The auto-start path uses `python -m clawd_tank_daemon.daemon` which calls `main()`. Since `main()` now uses `argparse`, calling with no args should work identically to before.

Run: `cd /Users/marciorodrigues/Projects/clawd-tank/host && .venv/bin/python -c "from clawd_tank_daemon.daemon import main; print('import ok')"`
Expected: `import ok`

---

### Task 9: End-to-end smoke test

**Files:** None (manual testing)

- [ ] **Step 1: Build simulator**

Run: `cd /Users/marciorodrigues/Projects/clawd-tank/simulator && cmake -B build && cmake --build build`
Expected: Clean build.

- [ ] **Step 2: Test simulator + netcat**

```bash
# Terminal 1:
./simulator/build/clawd-tank-sim --listen

# Terminal 2 (keep connection open 5s so UI is visible):
(echo '{"action":"add","id":"test-1","project":"clawd-tank","message":"Waiting for input"}'; sleep 5) | nc localhost 19872
```
Expected: Simulator shows connected state and notification card for 5 seconds, then disconnects when nc closes.

- [ ] **Step 3: Test simulator + daemon**

```bash
# Terminal 1:
./simulator/build/clawd-tank-sim --listen

# Terminal 2:
cd host && .venv/bin/python -m clawd_tank_daemon.daemon --sim-only

# Terminal 3:
echo '{"event":"add","session_id":"test-1","project":"my-app","message":"Hello"}' | nc -U ~/.clawd-tank/sock
```
Expected: Notification appears on simulator via daemon.

- [ ] **Step 4: Test headless + listen**

```bash
./simulator/build/clawd-tank-sim --listen --headless &
SIM_PID=$!
sleep 1  # Wait for simulator to bind port
(echo '{"action":"add","id":"t1","project":"test","message":"headless"}'; sleep 2) | nc localhost 19872
kill $SIM_PID
```
Expected: Simulator runs indefinitely until killed, accepts TCP connections.

- [ ] **Step 5: Test config read/write**

```bash
# With simulator running interactively (--listen):

# Write config:
(echo '{"action":"write_config","brightness":200,"sleep_timeout":60}'; sleep 1) | nc localhost 19872
# Should see [tcp] Config: brightness=200, sleep_timeout=60

# Read config (bidirectional — verify response):
(echo '{"action":"read_config"}'; sleep 1) | nc localhost 19872
# Should print: {"brightness":200,"sleep_timeout":60}
```

- [ ] **Step 5b: Test reconnect**

```bash
# Terminal 1: Start simulator
./simulator/build/clawd-tank-sim --listen

# Terminal 2: Start daemon
cd host && .venv/bin/python -m clawd_tank_daemon.daemon --sim-only

# Terminal 3: Send notification, then kill+restart simulator
echo '{"event":"add","session_id":"reconnect-test","project":"test","message":"Before restart"}' | nc -U ~/.clawd-tank/sock
# Kill simulator (Ctrl-C in Terminal 1), then restart it:
./simulator/build/clawd-tank-sim --listen
# Daemon should reconnect and replay the active notification
```
Expected: After simulator restarts, daemon reconnects and the "Before restart" notification reappears.

- [ ] **Step 6: Run full test suite one final time**

```bash
cd /Users/marciorodrigues/Projects/clawd-tank/host && .venv/bin/pytest -v
cd /Users/marciorodrigues/Projects/clawd-tank/firmware/test && make test
```
Expected: All tests PASS.

- [ ] **Step 7: Final commit if any fixups needed**

```bash
git add -u
git commit -m "fix: address issues found in smoke testing"
```
