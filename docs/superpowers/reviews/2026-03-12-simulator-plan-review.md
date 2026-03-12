# Plan Review: 2026-03-12-simulator.md

**Reviewer:** Claude Sonnet 4.6
**Date:** 2026-03-12
**Plan:** `docs/superpowers/plans/2026-03-12-simulator.md`
**Spec:** `docs/superpowers/specs/2026-03-12-simulator-design.md`

---

## Code Review Summary

The plan is well-structured, technically detailed, and covers nearly all spec requirements. The shims are correct, the API usages are valid for LVGL v9, the CMake structure is sound, and the task ordering is sensible. There are a handful of issues — one is a build-breaking API mismatch, two are spec deviations that will cause behavior differences, and several are minor polish items.

---

## Critical Issues

### ISSUE-1: `sim_display_set_quit` is not declared in `sim_display.h` when first used

**Location:** Task 4, Step 1 (`sim_display.c` code) vs Task 7, Step 2 (the fix)

`sim_display.c` defines `sim_display_set_quit()` at line 498 of the plan, and `sim_main.c` in Task 7 calls it — but `sim_display.h` in Task 4, Step 1 does not declare it. The plan defers adding the declaration to Task 7, Step 2, which means the build in Task 4 Step 5 will produce an implicit-function-declaration warning that becomes an error with `-Werror` (clang's default on macOS), or at minimum undefined behavior.

**Fix:** Add `void sim_display_set_quit(void);` to the `sim_display.h` block in Task 4, Step 1, not as a separate step in Task 7.

---

## High Priority Issues

### ISSUE-2: Spec says `LV_USE_SDL 1`, plan uses `LV_USE_SDL 0`

**Location:** `lv_conf.h` (Task 2, Step 1); Spec section "LVGL Configuration"

The spec explicitly lists `LV_USE_SDL 1` as a key setting. The plan sets `LV_USE_SDL 0` and adds a comment "Disable LVGL's SDL driver — we manage SDL2 directly." The plan's architecture note at the top is consistent with this (`LVGL's built-in SDL driver is NOT used`), but the spec's configuration table directly contradicts it.

This is almost certainly a spec error — the plan's approach (manual SDL2 management for keyboard event control) is correct and technically superior for this use case. However, an implementer who reads only the spec's config table will set `LV_USE_SDL 1` and get LVGL trying to register its own SDL display driver, conflicting with `sim_display.c`.

**Fix:** Update the spec's config table to read `LV_USE_SDL 0` and note the rationale.

### ISSUE-3: `LV_USE_FLOAT 1` specified in spec but missing from plan's `lv_conf.h`

**Location:** Task 2, Step 1 (`lv_conf.h`); Spec section "LVGL Configuration"

The spec lists `LV_USE_FLOAT 1` as a key setting. The plan's `lv_conf.h` does not include it. This setting defaults to `0` in `lv_conf_internal.h`. While none of the compiled firmware sources (`scene.c`, `notification_ui.c`, `ui_manager.c`) directly use float-dependent LVGL APIs in the reviewed code, omitting it creates a divergence from firmware behavior and may cause subtle rendering differences if LVGL's animation interpolation is float-gated.

**Fix:** Add `#define LV_USE_FLOAT 1` to the `lv_conf.h` block in Task 2, Step 1.

---

## Medium Priority Issues

### ISSUE-4: `LV_SDL_DIRECT_EXIT 0` and `LV_SDL_INCLUDE_PATH` from spec are absent from plan

**Location:** Task 2, Step 1 (`lv_conf.h`); Spec section "LVGL Configuration"

The spec lists two additional settings: `LV_SDL_DIRECT_EXIT 0` (prevent SDL from calling `exit(0)` on window close before screenshots are written) and `LV_SDL_INCLUDE_PATH <SDL2/SDL.h>`. Since the plan sets `LV_USE_SDL 0`, the SDL driver is disabled and both settings have no effect. However:

- `LV_SDL_DIRECT_EXIT 0` is a safety measure the spec author explicitly called out for a reason. If `LV_USE_SDL` is ever changed during debugging, the omission becomes dangerous.
- These settings document intent.

**Verdict:** OK to omit given `LV_USE_SDL 0`, but worth a comment in `lv_conf.h` explaining why they are not set.

### ISSUE-5: `wait` command in headless mode does not match spec behavior for simulated time

**Location:** `sim_events.c` (Task 6), `sim_main.c` headless loop (Task 7)

The spec states:
> Each `wait <N>` advances the tick counter by N ms and iterates `lv_timer_handler()` enough times (at 33ms steps) to process that simulated duration — this ensures animations, the 8s notification rotation timer, and the sleep timeout all fire at correct simulated times.

The plan implements `wait` in the inline event parser purely as `current_time += ms` — it does not call `lv_timer_handler()` at all during the wait. Instead, the headless main loop advances `TICK_MS` (33ms) per iteration and calls `ui_manager_tick()` (which calls `lv_timer_handler()` internally) once per step. This means:

- If events are spaced 8000ms apart with `wait 8000`, the loop does call `lv_timer_handler()` enough times (`8000 / 33 = ~242` iterations). So LVGL timers do fire correctly.
- The spec's description matches the plan's net behavior.

This is OK in practice, but only because the main loop iterates at 33ms steps. If `TICK_MS` were changed or the loop structure changed, the spec's per-wait guarantee would be lost. No code change needed, but the plan comment on `wait` parsing is misleading — it says "time advance, no event" without explaining that the main loop handles the tick advancement via regular iterations.

**Suggestion:** Add a comment in the headless main loop explaining that simulated time advances via TICK_MS steps, and that this is what drives `lv_timer_handler()` calls — not the `wait` parser itself.

### ISSUE-6: `maybe_capture_periodic` always starts capturing at `time = 0` regardless of `s_last_screenshot_time` initialization

**Location:** Task 7, Step 1, `maybe_capture_periodic()` function

`s_last_screenshot_time` is initialized to `0`. The headless loop calls `maybe_capture_periodic(0)` before the main loop, then starts advancing `time` by 33ms steps. This is correct for capturing a frame at `t=0`. However, if `--screenshot-interval 100` is set, the `while` loop inside `maybe_capture_periodic` will capture at `t=0`, `t=100`, `t=200`, etc. — which is the desired behavior.

There is a subtle issue: after the loop body runs for the first time capturing at `t=0`, `s_last_screenshot_time` becomes `100`. On the next call when `time=33`, the condition `s_last_screenshot_time (100) <= time_ms (33)` is false, so nothing fires. This is correct. No bug here.

OK as-is.

### ISSUE-7: `LV_FONT_MONTSERRAT_14` missing from `lv_conf.h`

**Location:** Task 2, Step 1 (`lv_conf.h`)

The plan's `lv_conf.h` enables `LV_FONT_MONTSERRAT_8`, `10`, `12`, `14`, `18`. Verified against `notification_ui.c` and `scene.c`:
- `notification_ui.c` uses: `14`, `12`, `10`
- `scene.c` uses: `18`, `14`

The plan includes all needed sizes (8, 10, 12, 14, 18). This is OK. The spec only lists 8, 10, 18 which is a spec omission — 12 and 14 are also needed. The plan is more correct than the spec here.

OK.

---

## Low Priority Suggestions

### SUGGESTION-1: `LV_BUILD_CONF_DIR` CACHE variable must be set before `add_subdirectory`

**Location:** Task 3, Step 1 (`CMakeLists.txt`)

The plan correctly sets `LV_BUILD_CONF_DIR` before `add_subdirectory(${LVGL_DIR} ...)`. Verified against `os_desktop.cmake`: the variable is read during that subdirectory's processing. Order is correct. No issue.

### SUGGESTION-2: CMake `CACHE PATH "" FORCE` may conflict if re-run with different config

**Location:** Task 3, Step 1 (`CMakeLists.txt`)

```cmake
set(LV_BUILD_CONF_DIR ${CMAKE_SOURCE_DIR} CACHE PATH "" FORCE)
```

Using `FORCE` is correct here to override any cached value from a previous run. This is intentional and appropriate. No issue.

### SUGGESTION-3: `cjson/` directory is not added to `target_include_directories` in the plan description

**Location:** Task 8, Step 2

The plan says: "Add `cjson/cJSON.c` to `add_executable` sources. Add `${CMAKE_SOURCE_DIR}/cjson` to `target_include_directories`." This is stated in prose but not in a code block. An implementer must manually add:

```cmake
target_include_directories(clawd-sim PRIVATE
    ...
    ${CMAKE_SOURCE_DIR}/cjson
)
```

This is only mentioned in text, not shown in a diff or snippet. Low risk since the instruction is clear, but a CMake snippet would be safer.

### SUGGESTION-4: Interactive `dismiss` by index (keys 1-8) uses `key_N` IDs that won't match

**Location:** Task 7, Step 1, `handle_sdl_events()` key handler

The `SDLK_1`-`SDLK_8` handler builds `evt.id` as `"key_0"`, `"key_1"`, etc. (using `idx = e.key.keysym.sym - SDLK_1`). But notifications added by pressing `n` use IDs like `"key_0"`, `"key_1"`, `"key_2"` etc. (using `s_sample_notif_idx`). So key `1` will dismiss notification `key_0` — the first one added — which is correct. Key `2` dismisses `key_1`, etc.

However, if the user presses `n` multiple times, the IDs are `key_0`, `key_1`, `key_2`... and pressing `1` dismisses `key_0`. This is index-to-ID mapping by convention, not by a stored list. It works only if the user adds notifications in order and dismisses in order. It will silently fail (no-op in `notif_store_dismiss`) if IDs don't match.

This is noted in the plan as "simplified for interactive" — acceptable as-is for a dev tool. No fix required.

### SUGGESTION-5: `strncpy` without null termination guarantee in shim `ble_service.h`

**Location:** Task 1, Step 5 (`ble_service.h` shim) and `sim_events.c`

`strncpy(s_notif_ids[s_notif_id_count], evt.id, NOTIF_MAX_ID_LEN - 1)` — `strncpy` does not guarantee null termination if the source fills the buffer. However, `evt.id` was populated with `snprintf(evt.id, sizeof(evt.id), "sim_%d", ...)` which always null-terminates, so the actual IDs are always short. No real risk here, but using `snprintf` or explicit null-termination would be more robust.

### SUGGESTION-6: `scenario` demo.json path in Task 8 test command uses wrong prefix

**Location:** Task 8, Step 5 test command

```bash
cd simulator && cmake -B build && cmake --build build
rm -rf shots && ./build/clawd-sim --headless \
  --scenario scenarios/demo.json \
```

This runs from `simulator/` (after `cd simulator`), so `scenarios/demo.json` is correct relative to that working directory. OK.

But Task 9, Step 4 test command (run from repo root) uses:
```bash
./simulator/build/clawd-sim --headless \
  --scenario simulator/scenarios/demo.json \
```

This is also correct relative to repo root. Both are consistent. OK.

---

## Positive Highlights

- The shim headers are minimal, correct, and well-reasoned. Providing `_lock_t` stubs in `FreeRTOS.h` (rather than shimming `sys/lock.h`) is the right approach — confirmed by inspecting `ui_manager.c` which uses `_lock_init`, `_lock_acquire`, `_lock_release` directly.

- The `ble_service.h` shim correctly uses `notification.h` constants (`NOTIF_MAX_ID_LEN`, etc.) instead of hardcoded sizes, matching the spec's explicit requirement.

- The shim enum values match `firmware/main/ble_service.h` exactly: same order, same names. Verified.

- The `ui_manager_init(void)` call in `sim_main.c` matches the actual signature in `firmware/main/ui_manager.h`. The spec incorrectly shows `ui_manager_init(display)` with a display argument — the plan is correct.

- `lv_tick_set_cb(sim_get_tick)` is a valid LVGL v9 API. `sim_get_tick` returns `uint32_t` which matches `lv_tick_get_cb_t typedef uint32_t (*)(void)`. Correct.

- `lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565)` and `LV_DISPLAY_RENDER_MODE_PARTIAL` are valid LVGL v9 APIs. Both verified in LVGL headers.

- `LVGL_DIR` points to `../firmware/managed_components/lvgl__lvgl/` which is the correct existing path (confirmed on disk).

- The task ordering is clean: shims first (Task 1) → LVGL config (Task 2) → CMake + smoke test (Task 3) → display backend (Task 4) → screenshots (Task 5) → events (Task 6) → full main loop (Task 7) → JSON (Task 8) → polish (Task 9). No forward dependencies.

- The `sdl_tick_cb` wrapper around `SDL_GetTicks()` is valid. `SDL_GetTicks()` returns `Uint32` (a typedef for `uint32_t`) — compatible with `lv_tick_get_cb_t`.

- Compiling assets from `${FIRMWARE_DIR}/assets` (the sprite headers) via `target_include_directories` is correct — `scene.c` uses `#include "assets/sprite_*.h"` relative paths, which requires `firmware/main/` on the include path (the plan uses `${FIRMWARE_DIR}`).

---

## Recommendations

1. **Fix ISSUE-1** (missing `sim_display_set_quit` declaration in Task 4 header) before implementing — it will cause a build failure on first attempt.

2. **Fix ISSUE-3** (add `LV_USE_FLOAT 1` to `lv_conf.h`) to match spec intent.

3. **Clarify the spec/plan conflict on `LV_USE_SDL`** (ISSUE-2) — the plan's approach is architecturally correct. The spec's configuration table entry should be treated as wrong; the plan's `LV_USE_SDL 0` should be followed.

4. The plan is otherwise ready to implement. All other issues are minor and will not block a successful first build.
