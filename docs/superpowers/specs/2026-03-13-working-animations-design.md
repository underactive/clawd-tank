# Working Animations Integration Design

## Overview

Extend Clawd Tank to show real-time working state animations driven by Claude Code session hooks. The tank becomes a workload meter: Clawd's animation reflects how many sessions are actively working and what phase they're in.

## Core Concepts

### Session-Driven State Model

The daemon tracks per-session state in a `dict[session_id, state]` and computes a single **display state** sent to the device. The device has no knowledge of individual sessions — it receives and renders a single animation command.

### Per-Session States (Daemon Side)

| State | Trigger | Meaning |
|---|---|---|
| `registered` | `SessionStart` | Session exists, user hasn't prompted yet |
| `thinking` | `UserPromptSubmit` | Claude is reasoning, no tools fired yet |
| `working` | `PreToolUse` | Claude is executing tools |
| `idle` | `Stop` | Claude finished, waiting for user input |
| `confused` | `Notification` (idle_prompt) | Claude has been waiting 60s+ for input |

### Display States (Device Side)

| Display State | Condition | Animation |
|---|---|---|
| `sleeping` | No sessions registered | Sleeping (existing) |
| `idle` | 1+ sessions, all idle/registered | Idle (existing) |
| `thinking` | Any session thinking, none working | Thinking (new) |
| `working_1` | 1 session working | Typing (new) |
| `working_2` | 2 sessions working | Juggling (new) |
| `working_3` | 3+ sessions working | Building (new) |
| `confused` | Any session confused, none thinking/working | Confused (new) |
| `sweeping` | PreCompact oneshot interrupt | Sweeping (new) |

### Priority Resolution

The daemon evaluates all session states top-to-bottom, first match wins:

1. Count sessions in `working` state → `working_N` (intensity tier, capped at 3)
2. Any session `thinking`? → `thinking`
3. Any session `confused`? → `confused`
4. Any session `idle` or `registered`? → `idle`
5. No sessions? → `sleeping`

Note: `confused` is only visible when no sessions are actively working or thinking. If one session is confused but another is working, the working animation takes priority — the confused state is effectively masked until the higher-priority work completes.

The `registered` state (from `SessionStart`) is functionally equivalent to `idle` for display purposes — it means a session exists but nothing is happening yet.

The display state is only sent to the device when the computed result differs from the previously sent state.

### Special Events

- **PreCompact** → Oneshot `sweeping` interrupt. The daemon sends `set_status: sweeping` followed immediately by `set_status: <computed_state>`. The firmware plays the sweeping oneshot and uses the second status as the fallback animation when it completes. No daemon-side timer needed.
- **New notification** (`Stop` hook, currently adds a notification card) → Existing ALERT oneshot plays, then the scene animation falls back to the current session-derived display state (not always IDLE as it does today).

## Session Lifecycle

```
SessionStart → registered
UserPromptSubmit → thinking
PreToolUse → working (first tool call transitions from thinking)
Stop → idle (Claude finished generating)
Notification(idle_prompt) → confused (60s with no user input)
UserPromptSubmit → thinking (user submits again, cycle repeats)
SessionEnd → session removed from dict
```

### Staleness Timeout

If no events are received from a session within the configurable timeout (default 10 minutes), the daemon evicts it from the session dict. This handles cases where `SessionEnd` never fires (terminal killed, laptop sleeps, etc.).

This replaces the current sleep timer in `ui_manager.c`. The existing `sleep_timeout` config value is repurposed: instead of "time before Clawd sleeps," it becomes "time before an idle session is considered dead." Same menu bar UI, same BLE config characteristic, different semantics.

When the last session is evicted, the display state becomes `sleeping`.

## Data Flow

### Hook Events → Daemon

```
Claude Code hook (stdin JSON)
    → ~/.clawd-tank/clawd-tank-notify
    → Unix socket → daemon._handle_message()
```

The notify script and daemon protocol gain new event types:

| Hook Event | Daemon Event |
|---|---|
| `SessionStart` | `{"event": "session_start", "session_id": "..."}` |
| `UserPromptSubmit` | `{"event": "prompt_submit", "session_id": "..."}` |
| `PreToolUse` | `{"event": "tool_use", "session_id": "..."}` |
| `PreCompact` | `{"event": "compact", "session_id": "..."}` |
| `Stop` | `{"event": "add", "hook": "Stop", ...}` (adds notification card) |
| `Notification` (idle_prompt) | `{"event": "add", "hook": "Notification", ...}` (adds notification card) |
| `SessionEnd` | `{"event": "dismiss", "hook": "SessionEnd", ...}` (dismisses notification) |

Events that affect both notifications and session state include a `"hook"` discriminator field so the daemon can determine the correct session state transition. No multi-message protocol change needed — a single message carries both concerns:

- `Stop` → `{"event": "add", "hook": "Stop", "session_id": "...", ...}` — daemon adds notification card AND sets session state to `idle`.
- `Notification` (idle_prompt) → `{"event": "add", "hook": "Notification", "session_id": "...", ...}` — daemon adds notification card AND sets session state to `confused`.
- `UserPromptSubmit` → `{"event": "dismiss", "hook": "UserPromptSubmit", "session_id": "..."}` — daemon dismisses notification AND sets session state to `thinking`.
- `SessionEnd` → `{"event": "dismiss", "hook": "SessionEnd", "session_id": "..."}` — daemon dismisses notification AND removes session from dict.

The `"hook"` field disambiguates events that share the same `"event"` type but require different session state transitions. The daemon's `_handle_message()` performs the notification operation based on `"event"` and the session state update based on `"hook"`.

### Daemon → Device

New BLE/TCP action:

```json
{"action": "set_status", "status": "<display_state>"}
```

Where `<display_state>` is one of: `sleeping`, `idle`, `thinking`, `working_1`, `working_2`, `working_3`, `confused`, `sweeping`.

This is sent alongside (not replacing) existing `add`/`dismiss`/`clear` actions. The device processes them independently — `set_status` controls the scene animation, `add`/`dismiss`/`clear` control the notification cards.

### Device State Machine

The firmware's `ui_manager` gains awareness of the display status:

- A new `s_display_status` variable tracks the last received status.
- `BLE_EVT_SET_STATUS` is a new event type in `ble_evt_type_t`.
- When `set_status` is received, `ui_manager` maps the status string to the corresponding `clawd_anim_id_t` and calls `scene_set_clawd_anim()`.
- The `sweeping` status is treated as a oneshot — when it finishes, the scene auto-returns to the animation corresponding to `s_display_status` (not IDLE).
- The existing oneshot-to-IDLE fallback in `scene_tick()` is changed: oneshots now fall back to the animation mapped from `s_display_status` instead of always `CLAWD_ANIM_IDLE`.

## Layout Model

Two independent axes control the display:

1. **Layout** (notification-driven): Full-width (320px) when no notifications, split (107px + cards) when notifications exist. Unchanged from current behavior.
2. **Animation** (session-driven): Determined by the display state. Applies regardless of layout.

After the existing ALERT oneshot plays for a new notification, the scene animation returns to the current session-derived state (thinking, working, idle, etc.) instead of always IDLE.

### Time Display

The clock is shown whenever Clawd is in full-width layout, regardless of the animation state (idle, thinking, working, etc.). Previously it only showed during `UI_STATE_FULL_IDLE`.

## Sleep Model Change

The current timer-based sleep (5-minute inactivity in `ui_manager_tick()`) is removed. Sleep is now purely session-driven:

- **Sleep**: When the session dict is empty (no active sessions, all ended or evicted by staleness timeout).
- **Wake**: When any session event is received (SessionStart, or any event that implicitly creates a session entry).
- The `sleep_timeout` config value is repurposed for session staleness eviction.

The firmware no longer manages sleep timing — it simply renders whatever display state the daemon sends, including `sleeping`.

## BLE Disconnect Handling

The `CLAWD_ANIM_DISCONNECTED` state remains firmware-autonomous — triggered by the GAP callback when BLE connection drops, independent of `set_status`. On BLE disconnect, the firmware shows `CLAWD_ANIM_DISCONNECTED` regardless of any previously received display state. On reconnect, the daemon sends the current display state (along with replaying active notifications), which overrides the disconnected animation.

The `UI_STATE_DISCONNECTED` enum value and its transition logic remain unchanged.

## PreToolUse Frequency

`PreToolUse` fires for every tool call — a single turn can invoke 20+ tools. Most of these will be redundant (session is already in `working` state). This is acceptable overhead:

- The notify script is lightweight (stdlib only, ~100 lines).
- Each socket message is trivial to process.
- The daemon no-ops when the computed display state hasn't changed.

No filtering or caching in the notify script. The daemon's "only send when changed" guard handles deduplication naturally.

## Implicit Session Creation

If the daemon receives an event with a `session_id` that is not in the session dict (e.g., `SessionStart` hook is not registered in older Claude Code versions, or the daemon restarted mid-session), the session is implicitly created with the appropriate state for that event. For example, a `tool_use` event for an unknown session creates it in `working` state. This ensures robustness against missed lifecycle events.

## New Hooks Registration

The hook installer (`hooks.py` and the standalone notify script) must register these additional hooks:

```json
{
  "SessionStart": [
    {"hooks": [{"type": "command", "command": "~/.clawd-tank/clawd-tank-notify"}]}
  ],
  "PreToolUse": [
    {"hooks": [{"type": "command", "command": "~/.clawd-tank/clawd-tank-notify"}]}
  ],
  "PreCompact": [
    {"hooks": [{"type": "command", "command": "~/.clawd-tank/clawd-tank-notify"}]}
  ]
}
```

These are added to the existing `Stop`, `Notification`, `UserPromptSubmit`, and `SessionEnd` hooks.

Note: `PostToolUse` is NOT registered. It provides no additional signal — the transition from `working` back to `idle` is handled by `Stop`.

## New Sprite Assets Required

| Animation | Type | Sprite Name | Notes |
|---|---|---|---|
| Thinking | Looping | `sprite_thinking` | Clawd tapping chin, thought bubble |
| Typing | Looping | `sprite_typing` | Calm focused typing |
| Juggling | Looping | `sprite_juggling` | Juggling multiple data packets |
| Building | Looping | `sprite_building` | Hammering/constructing |
| Confused | Looping | `sprite_confused` | Looking around with question marks |
| Sweeping | Oneshot | `sprite_sweeping` | Push broom clearing data |

All new animations are looping except Sweeping. The existing Idle, Alert, Happy, Sleeping, and Disconnected animations are unchanged.

### Flash Budget

The existing 5 sprite headers occupy approximately 900KB of flash (RLE-compressed RGB565 const data). The ESP32-C6 has 8MB flash. After ESP-IDF runtime, NimBLE, LVGL, and existing sprites, there is ample headroom. Six new animations at a similar size (~150-200KB each) would add roughly 1-1.2MB, bringing total sprite data to ~2MB — well within the 8MB budget. Exact sizes will depend on frame count and complexity of each animation.

### Unused SVG Animations

The `assets/svg-animations/` directory contains 12 working animation SVGs. This spec uses 6 of them. The remaining animations are reserved for potential future use:

| SVG | Status | Notes |
|---|---|---|
| `clawd-working-thinking.svg` | **Used** | Thinking display state |
| `clawd-working-typing.svg` | **Used** | Working tier 1 |
| `clawd-working-juggling.svg` | **Used** | Working tier 2 |
| `clawd-working-building.svg` | **Used** | Working tier 3 |
| `clawd-working-confused.svg` | **Used** | Confused display state |
| `clawd-working-sweeping.svg` | **Used** | PreCompact oneshot |
| `clawd-working-carrying.svg` | Reserved | — |
| `clawd-working-conducting.svg` | Reserved | — |
| `clawd-working-debugger.svg` | Reserved | — |
| `clawd-working-overheated.svg` | Reserved | — |
| `clawd-working-pushing.svg` | Reserved | — |
| `clawd-working-wizard.svg` | Reserved | — |

## Firmware Changes Summary

### `scene.h` / `scene.c`

- Add 6 new values to `clawd_anim_id_t` enum.
- Add 6 entries to `anim_defs[]` table.
- Include 6 new sprite headers.
- Add new public API `void scene_set_fallback_anim(scene_t *scene, clawd_anim_id_t anim)` — sets which animation oneshots return to when they complete. Add `s_fallback_anim` field to scene state. Initial value at boot: `CLAWD_ANIM_IDLE`.
- Change oneshot fallback in `scene_tick()`: instead of always returning to `CLAWD_ANIM_IDLE`, return to `s_fallback_anim`.

### `ble_service.h` / `ble_service.c`

- Add `BLE_EVT_SET_STATUS` to `ble_evt_type_t`.
- Add a `uint8_t status` field to `ble_evt_t`. The status is a numeric enum (`display_status_t`) mapped from the JSON string in `parse_notification_json()`. This avoids passing strings through the FreeRTOS queue and eliminates string comparisons in `ui_manager`.
- Define `display_status_t` enum:
  ```c
  typedef enum {
      DISPLAY_STATUS_SLEEPING,
      DISPLAY_STATUS_IDLE,
      DISPLAY_STATUS_THINKING,
      DISPLAY_STATUS_WORKING_1,
      DISPLAY_STATUS_WORKING_2,
      DISPLAY_STATUS_WORKING_3,
      DISPLAY_STATUS_CONFUSED,
      DISPLAY_STATUS_SWEEPING,
  } display_status_t;
  ```
- Add `"set_status"` action to `parse_notification_json()`: parse `"status"` string, map to `display_status_t`, post `BLE_EVT_SET_STATUS`.

### `ui_manager.c`

- Handle `BLE_EVT_SET_STATUS`: map `display_status_t` to `clawd_anim_id_t`, update `s_display_status`, call `scene_set_fallback_anim()`. If a oneshot animation is currently playing (`scene_is_playing_oneshot()`), do NOT call `scene_set_clawd_anim()` — only update the fallback so the oneshot completes naturally and returns to the correct state. Otherwise, call `scene_set_clawd_anim()` to switch immediately.
- Remove the sleep timer logic from `ui_manager_tick()`.
- When `set_status` is `sleeping`, set `CLAWD_ANIM_SLEEPING` and turn off backlight (`display_set_brightness(0)`).
- When `set_status` transitions away from `sleeping`, restore backlight to the config-stored brightness value (`config_store_get_brightness()`).
- Change `transition_to(UI_STATE_FULL_IDLE)`: instead of setting IDLE animation, set the animation from `s_display_status`.
- Show time display whenever in full-width layout: move the time update logic from checking `UI_STATE_FULL_IDLE` to checking whether notifications are present (i.e., full-width layout). Consider renaming `UI_STATE_FULL_IDLE` to `UI_STATE_FULL` since it now covers idle, thinking, working, and sleeping states.

### `sim_ble_parse.c`

- Mirror the `"set_status"` action parsing from `ble_service.c`.

## Host Changes Summary

### `clawd-tank-notify` (three code locations)

There are three places where hook-to-message logic lives:

1. **`protocol.py`** (`hook_payload_to_daemon_message()`) — canonical implementation. All logic changes go here first.
2. **Development `clawd-tank-notify`** (`host/clawd-tank-notify`) — imports from `protocol.py`, gets changes for free.
3. **Standalone `NOTIFY_SCRIPT`** (embedded in `hooks.py`) — stdlib-only copy, must be manually synced with `protocol.py` after changes.

Changes:
- Handle new hook events: `SessionStart`, `PreToolUse`, `PreCompact`.
- Produce new daemon event types: `session_start`, `prompt_submit`, `tool_use`, `compact`.
- Add `"hook"` discriminator field to existing `add`/`dismiss` events so the daemon can distinguish `Stop` vs `Notification` (idle vs confused) and `UserPromptSubmit` vs `SessionEnd` (thinking vs remove).

### `protocol.py`

- `hook_payload_to_daemon_message()`: Handle new hook event types (`SessionStart`, `PreToolUse`, `PreCompact`). Existing events (`Stop`, `Notification`, `UserPromptSubmit`, `SessionEnd`) already include `session_id` in their messages — no change needed for those.
- `daemon_message_to_ble_payload()`: Handle new event types (`session_start`, `tool_use`, `compact`) — these do NOT produce BLE payloads (they are daemon-internal for session tracking). The daemon sends `set_status` BLE payloads separately via the session state tracker, not through the normal message-to-payload conversion.

### `daemon.py`

- Add `_session_states: dict[str, SessionState]` tracking per-session state and last-event timestamp.
- Add `_compute_display_state()` method implementing the priority resolution logic.
- On each session state change: compute new display state, if different from previous, broadcast `set_status` to all transports.
- Add staleness eviction: periodic check (e.g., every 30s) removes sessions whose last-event timestamp exceeds `sleep_timeout`. When the last session is evicted, send `sleeping` status.
- Handle `compact` events: send `set_status: sweeping` immediately, then send `set_status: <computed_state>` right after. The firmware handles the oneshot-to-fallback transition.
- On transport reconnect: send current display state in addition to replaying active notifications.

### `hooks.py`

- Add `SessionStart`, `PreToolUse`, and `PreCompact` to `HOOKS_CONFIG`.

### Menu bar app

- Rename sleep timeout label to "Session timeout" with the meaning: "Sessions without activity are removed after this duration."

## BLE Payload Size

The largest `set_status` payload is `{"action":"set_status","status":"working_3"}` at 46 bytes — well within the 256-byte BLE MTU constraint.

## Test Plan

### Host (Python)

- **Unit tests for `_compute_display_state()`**: Test all priority resolution cases — single session in each state, multiple sessions with mixed states, empty session dict, working count tiers (1, 2, 3+).
- **Unit tests for session staleness eviction**: Verify sessions are evicted after timeout, last eviction triggers `sleeping`, new events for evicted sessions re-create them.
- **Protocol tests**: Verify new event types (`session_start`, `tool_use`, `compact`) are correctly produced by `hook_payload_to_daemon_message()`. Verify existing events (`add`, `dismiss`) still work unchanged and their `session_id` is used for session state updates.
- **Integration tests**: Use the simulator with `--headless --listen` to verify end-to-end flow from hook payload to display state change.

### Firmware (C)

- **Unit tests for `set_status` parsing**: Add test cases to the existing test suite for `parse_notification_json()` in both `ble_service.c` and `sim_ble_parse.c`. Verify all status strings map to correct `display_status_t` values. Verify unknown status strings are rejected gracefully.
- **Simulator scenario tests**: Create scenario JSON files that exercise the full session lifecycle (session start → prompt → tool use → stop → end) and verify correct animation transitions via screenshots.
