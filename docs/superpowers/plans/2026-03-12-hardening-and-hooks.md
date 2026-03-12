# Hardening & Hook Auto-Install Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the ValueError crash in the Python daemon's BLE sender, add all missing error checks in firmware, and make the hook install script auto-merge into Claude Code settings.

**Architecture:** Three independent streams — a Python try/except in the daemon, mechanical ESP-IDF error-check wrappers in three C files, and a jq-based JSON merge in the shell script. All are leaf changes with no cross-dependencies.

**Tech Stack:** Python 3 (asyncio, pytest), ESP-IDF 5.3.2 C (NimBLE, LVGL, FreeRTOS), Bash + jq

---

## Chunk 1: Python ValueError Crash Fix

### Task 1: Guard `_ble_sender` against ValueError from unknown events

**Files:**
- Modify: `host/clawd_tank_daemon/daemon.py:77`
- Test: `host/tests/test_daemon.py`

- [ ] **Step 1: Write the failing test**

Add to `host/tests/test_daemon.py`:

```python
@pytest.mark.asyncio
async def test_unknown_event_does_not_crash_sender():
    """An unknown event in the queue must be logged and skipped, not crash _ble_sender."""
    daemon = ClawdDaemon()
    # Queue an unknown event followed by a valid dismiss
    await daemon._handle_message({"event": "bogus", "session_id": "x"})
    await daemon._handle_message({"event": "dismiss", "session_id": "x"})

    # _ble_sender should process both without raising.
    # We can't easily run the full sender loop, so call the conversion directly
    # to confirm the ValueError path exists, then verify the daemon handles it.
    from clawd_tank_daemon.protocol import daemon_message_to_ble_payload
    with pytest.raises(ValueError):
        daemon_message_to_ble_payload({"event": "bogus"})

    # The queue should have both messages
    assert daemon._pending_queue.qsize() == 2
```

- [ ] **Step 2: Run test to verify it passes (baseline)**

Run: `cd host && pytest tests/test_daemon.py::test_unknown_event_does_not_crash_sender -v`
Expected: PASS — this test confirms the ValueError exists but doesn't yet test the sender loop guard.

- [ ] **Step 3: Write an integration-style test for the sender loop**

Add `import asyncio` and `from unittest.mock import AsyncMock` to the imports at the top of `host/tests/test_daemon.py`, then add the test:

```python
@pytest.mark.asyncio
async def test_ble_sender_skips_unknown_event():
    """_ble_sender must skip unknown events and continue processing the queue."""
    daemon = ClawdDaemon()
    daemon._ble = AsyncMock()
    daemon._ble.ensure_connected = AsyncMock()
    daemon._ble.write_notification = AsyncMock(return_value=True)

    # Queue: unknown event, then a valid dismiss
    await daemon._pending_queue.put({"event": "bogus", "session_id": "x"})
    await daemon._pending_queue.put({"event": "dismiss", "session_id": "d1"})

    # Run sender briefly — it should process both without crashing
    sender = asyncio.create_task(daemon._ble_sender())
    await asyncio.sleep(0.1)  # Let it drain the queue
    daemon._running = False
    sender.cancel()
    try:
        await sender
    except asyncio.CancelledError:
        pass

    # The valid dismiss should have been sent (write_notification called at least once)
    assert daemon._ble.write_notification.call_count >= 1
```

- [ ] **Step 4: Run test to verify it fails**

Run: `cd host && pytest tests/test_daemon.py::test_ble_sender_skips_unknown_event -v`
Expected: FAIL — `ValueError: Unknown event: bogus` kills the sender task.

- [ ] **Step 5: Implement the fix**

In `host/clawd_tank_daemon/daemon.py`, wrap the `daemon_message_to_ble_payload` call in `_ble_sender` with a try/except:

```python
    async def _ble_sender(self) -> None:
        """Process pending messages and send them over BLE."""
        while self._running:
            try:
                msg = await asyncio.wait_for(self._pending_queue.get(), timeout=1.0)
            except asyncio.TimeoutError:
                continue
            await self._ble.ensure_connected()

            try:
                payload = daemon_message_to_ble_payload(msg)
            except ValueError:
                logger.error("Skipping unknown event: %s", msg.get("event"))
                continue

            success = await self._ble.write_notification(payload)

            if not success:
                await self._ble.ensure_connected()
                await self._replay_active()
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cd host && pytest tests/test_daemon.py -v`
Expected: ALL PASS

- [ ] **Step 7: Run full Python test suite**

Run: `cd host && pytest -v`
Expected: ALL PASS

- [ ] **Step 8: Commit**

```bash
git add host/clawd_tank_daemon/daemon.py host/tests/test_daemon.py
git commit -m "fix: catch ValueError in _ble_sender for unknown events

An unknown event type passed through _handle_message would raise
ValueError in daemon_message_to_ble_payload, killing the sender
loop permanently. Now logs the error and continues."
```

---

## Chunk 2: Firmware Hardening

### Task 2: Add error checks to `ble_service.c`

**Files:**
- Modify: `firmware/main/ble_service.c:105,179,195,199-200`

- [ ] **Step 1: Check `ble_hs_mbuf_to_flat` return value (line 105)**

Change in `notification_write_cb`:

```c
    char buf[513];
    uint16_t copied;
    int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, len, &copied);
    if (rc != 0) {
        ESP_LOGW(TAG, "mbuf_to_flat failed: %d", rc);
        return BLE_ATT_ERR_UNLIKELY;
    }
    buf[copied] = '\0';
```

- [ ] **Step 2: Check `ble_svc_gap_device_name_set` and GATT registration (lines 195, 199-200)**

Change in `ble_service_init` — declare `int rc` once and reuse for all three calls:

```c
    int rc = ble_svc_gap_device_name_set("Clawd Tank");
    if (rc != 0) {
        ESP_LOGW(TAG, "Failed to set GAP device name: %d", rc);
    }

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT count_cfg failed: %d", rc);
        abort();
    }
    rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT add_svcs failed: %d", rc);
        abort();
    }
```

- [ ] **Step 3: Add `ble_hs_util_ensure_addr(0)` before `start_advertising()` in `ble_on_sync` (line 179)**

```c
static void ble_on_sync(void) {
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to ensure address: %d", rc);
        return;
    }
    ESP_LOGI(TAG, "BLE synced, starting advertising as 'Clawd Tank'");
    start_advertising();
}
```

- [ ] **Step 4: Commit ble_service.c changes**

```bash
git add firmware/main/ble_service.c
git commit -m "fix(ble): add missing error checks in ble_service.c

- Check ble_hs_mbuf_to_flat return before using copied length
- Check ble_svc_gap_device_name_set return value
- Abort on ble_gatts_count_cfg/add_svcs failure (fatal misconfiguration)
- Call ble_hs_util_ensure_addr(0) before start_advertising"
```

### Task 3: Add error checks to `display.c`

**Files:**
- Modify: `firmware/main/display.c:128,146,152`

- [ ] **Step 1: NULL-check `lv_display_create` (line 146)**

```c
    lv_display_t *display = lv_display_create(LCD_H_RES, LCD_V_RES);
    if (!display) {
        ESP_LOGE(TAG, "lv_display_create failed — out of memory");
        abort();
    }
```

- [ ] **Step 2: Replace bare `assert` with `configASSERT` for DMA buffers (lines 128, 152)**

Line 128 (clear buffer):
```c
        void *clear_buf = heap_caps_calloc(1, clear_sz, MALLOC_CAP_DMA);
        configASSERT(clear_buf);
```

Line 152 (LVGL draw buffers):
```c
    void *buf1 = heap_caps_malloc(buf_sz, MALLOC_CAP_DMA);
    void *buf2 = heap_caps_malloc(buf_sz, MALLOC_CAP_DMA);
    configASSERT(buf1 && buf2);
```

- [ ] **Step 3: Commit display.c changes**

```bash
git add firmware/main/display.c
git commit -m "fix(display): add NULL checks and use configASSERT for DMA buffers

- NULL-check lv_display_create and abort on failure
- Replace bare assert() with configASSERT() (idiomatic FreeRTOS pattern)"
```

### Task 4: Add `xTaskCreate` return check in `main.c`

**Files:**
- Modify: `firmware/main/main.c:48`

- [ ] **Step 1: Check xTaskCreate return value**

```c
    BaseType_t ret = xTaskCreate(ui_task, "ui_task", 8192, NULL, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create ui_task");
        abort();
    }
```

- [ ] **Step 2: Commit main.c change**

```bash
git add firmware/main/main.c
git commit -m "fix(main): check xTaskCreate return value

Silent task creation failure would leave the device advertising
but with no UI task running."
```

- [ ] **Step 3: Build firmware to verify no compile errors**

Run: `cd firmware && idf.py build`
Expected: Build succeeds with no errors or new warnings.

---

## Chunk 3: Hook Auto-Install

### Task 5: Make `install-hooks.sh` auto-merge hooks into settings.json

**Files:**
- Modify: `host/install-hooks.sh`

- [ ] **Step 1: Rewrite the script to use jq for JSON merging**

Replace the full contents of `host/install-hooks.sh`:

```bash
#!/bin/bash
# install-hooks.sh — Installs Clawd Tank notification hooks into Claude Code settings.
# Usage: ./install-hooks.sh

set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CLAWD_NOTIFY="$SCRIPT_DIR/clawd-tank-notify"
SETTINGS_FILE="${SETTINGS_FILE:-$HOME/.claude/settings.json}"

if [ ! -f "$CLAWD_NOTIFY" ]; then
    echo "Error: clawd-tank-notify not found at $CLAWD_NOTIFY"
    exit 1
fi

if ! command -v jq &>/dev/null; then
    echo "Error: jq is required but not installed. Install with: brew install jq"
    exit 1
fi

# Build the hooks JSON
HOOKS_JSON=$(cat <<ENDJSON
{
  "Notification": [
    {
      "matcher": "idle_prompt",
      "hooks": [
        {
          "type": "command",
          "command": "$CLAWD_NOTIFY"
        }
      ]
    }
  ],
  "UserPromptSubmit": [
    {
      "hooks": [
        {
          "type": "command",
          "command": "$CLAWD_NOTIFY"
        }
      ]
    }
  ],
  "SessionEnd": [
    {
      "hooks": [
        {
          "type": "command",
          "command": "$CLAWD_NOTIFY"
        }
      ]
    }
  ]
}
ENDJSON
)

# Create settings file if it doesn't exist
if [ ! -f "$SETTINGS_FILE" ]; then
    mkdir -p "$(dirname "$SETTINGS_FILE")"
    echo '{}' > "$SETTINGS_FILE"
fi

# Merge hooks into existing settings (preserves all other keys)
UPDATED=$(jq --argjson hooks "$HOOKS_JSON" '.hooks = (.hooks // {}) * $hooks' "$SETTINGS_FILE")
echo "$UPDATED" > "$SETTINGS_FILE"

echo "Clawd Tank hooks installed into $SETTINGS_FILE"
echo "Hook command: $CLAWD_NOTIFY"
echo ""
echo "NOTE: The 'matcher' field filters which notification types trigger the hook."
echo "If your Claude Code version doesn't support 'matcher', remove it —"
echo "clawd-tank-notify already filters by notification_type in protocol.py."
```

- [ ] **Step 2: Verify the script is executable**

Run: `chmod +x host/install-hooks.sh`

- [ ] **Step 3: Test the script with a dry run**

Test that jq merge logic works correctly by running with a temp settings file:

```bash
TEMP_SETTINGS=$(mktemp)
echo '{"env": {"FOO": "1"}, "statusLine": {"type": "command"}}' > "$TEMP_SETTINGS"
SETTINGS_FILE="$TEMP_SETTINGS" host/install-hooks.sh
cat "$TEMP_SETTINGS"  # should show merged JSON with hooks + original keys preserved
rm "$TEMP_SETTINGS"
```

Expected: The temp file contains both the original `env`/`statusLine` keys AND the new `hooks` key.

- [ ] **Step 4: Commit**

```bash
git add host/install-hooks.sh
git commit -m "feat: auto-merge hooks into Claude Code settings.json

install-hooks.sh now uses jq to merge Clawd Tank hooks into
~/.claude/settings.json, preserving existing settings. Also adds
set -u for safer variable handling.

Requires: jq (brew install jq)"
```

---

## Chunk 4: Final Verification & TODO Update

### Task 6: Update TODO.md

**Files:**
- Modify: `TODO.md`

- [ ] **Step 1: Check off completed items**

Mark these as done (`[x]`):
- `_ble_sender ValueError crash` (#18)
- All 7 firmware hardening items (#11-17)
- `Install Claude Code hooks` (#9)
- `install-hooks.sh add set -u` (#31)

- [ ] **Step 2: Commit**

```bash
git add TODO.md
git commit -m "docs: update TODO.md — mark hardening and hook items complete"
```
