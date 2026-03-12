# Simulator Design Spec Review
**Spec:** `docs/superpowers/specs/2026-03-12-simulator-design.md`
**Reviewer:** Claude Sonnet 4.6
**Date:** 2026-03-12

---

## Summary

The spec is well-structured and covers the primary concerns correctly. The shim strategy is sound, the LVGL v9.5 SDL2 driver support is confirmed to exist in the managed component, and the four firmware source files are clean of FreeRTOS and BLE dependencies in their bodies. However, there are several blocking issues that would cause a build failure or silent runtime bugs, plus a few important gaps that need resolution before handing this to an implementer.

---

## ISSUE (must fix)

### 1. `sys/lock.h` shim will be shadowed by macOS SDK — `_lock_t` will be undefined

The spec places the shim at `simulator/shims/sys/lock.h`. The CMake include order puts `simulator/shims/` first. However, macOS ships its own `/usr/include/sys/lock.h` (confirmed present at `/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/sys/lock.h`). Because `ui_manager.c` does not include `sys/lock.h` directly — it uses `_lock_t` as a bare type — the type must be defined before any firmware header is parsed.

The critical include chain is:
```
ui_manager.h -> ble_service.h -> freertos/FreeRTOS.h (shim)
```
The `_lock_t` type is not declared in any of those headers. In the real ESP-IDF, `_lock_t` comes from the Newlib `<sys/lock.h>` which ships with the toolchain. On macOS, the system `sys/lock.h` is a near-empty BSD stub that does NOT define `_lock_t`.

**Consequence:** `ui_manager.c` will fail to compile with `error: unknown type name '_lock_t'`.

**Fix:** The `shims/freertos/FreeRTOS.h` must also define `_lock_t`, or there must be a top-level `shims/sys/lock.h` that is reachable via `#include <sys/lock.h>`. Since the shim path `simulator/shims/` is on the include path, `#include <sys/lock.h>` will resolve to the system SDK, not the shim, unless the shim directory is also added as a system include or the shim is placed at the path that gets picked up first.

The safest fix: declare `typedef int _lock_t;` inside `shims/freertos/FreeRTOS.h` itself, removing the dependency on a `sys/lock.h` shim entirely. The `sys/lock.h` shim entry in the spec's directory tree can then be removed.

---

### 2. `ble_service.h` shim uses hardcoded field sizes instead of the constants from `notification.h`

The spec defines the shim `ble_service.h` with:
```c
char id[48];
char project[32];
char message[64];
```

The real `ble_service.h` defines the struct as:
```c
char id[NOTIF_MAX_ID_LEN];       // 48
char project[NOTIF_MAX_PROJ_LEN]; // 32
char message[NOTIF_MAX_MSG_LEN];  // 64
```
where those constants come from `notification.h`.

While the numbers happen to match today, this is a fragile coupling. If a developer changes `NOTIF_MAX_MSG_LEN` in `notification.h`, the shim will silently produce a struct with a different layout than `ui_manager.c` expects, causing memory corruption at runtime with no compile error.

**Fix:** The shim must `#include "notification.h"` and use the same constants, exactly as the real `ble_service.h` does.

---

### 3. `lv_timer_handler()` is called twice per loop — once inside `ui_manager_tick()` and once from the SDL driver

`ui_manager_tick()` (line 239 of `ui_manager.c`) explicitly calls `lv_timer_handler()`. In the real firmware this is the only call site. However, in the simulator, if the sim main loop calls `ui_manager_tick()` AND the LVGL SDL driver is active, `lv_timer_handler()` will be called twice per iteration: once from the simulator's main loop and once internally by the SDL driver's own LVGL timer (`sdl_event_handler` is registered at 5ms via `lv_timer_create`).

`lv_timer_handler()` is not idempotent — calling it twice rapidly will double-fire all LVGL timers including the notification rotation timer (8s interval) and the SDL event handler timer itself.

**Fix:** The spec's `sim_main.c` loop must NOT call `ui_manager_tick()` for its `lv_timer_handler()` side effect when SDL is active. Two options:
- Option A: `sim_main.c` calls `lv_timer_handler()` directly and calls scene_tick + sleep-timeout logic separately, bypassing `ui_manager_tick()`.
- Option B: Document that `sim_display_tick()` calls `lv_timer_handler()` and `ui_manager_tick()` should be restructured so it does not call `lv_timer_handler()` in its SDL build variant. Since the spec requires no changes to firmware sources, option A is the correct approach. The spec's `sim_main.c` description needs to spell this out.

---

### 4. Headless mode tick source is underspecified — `lv_tick_get()` will return wall-clock time, not simulated time

The spec says headless mode uses "simulated time advances per frame" and "LVGL tick driven by `gettimeofday()` instead of real-time." But there is no spec for how simulated time is connected to `lv_tick_get()`.

`lv_tick_get()` in LVGL v9 can be redirected via `lv_tick_set_cb(my_cb)` (confirmed in `lv_tick.h`). Without this redirection, `lv_tick_get()` uses an internal counter that advances only when `lv_tick_inc()` is called, or falls back to a platform default. If the headless simulator does not call `lv_tick_set_cb()` with a simulated-time provider, then:
- `lv_tick_get()` will use real wall time.
- `wait 500` will mean "advance simulated event clock by 500ms" but LVGL's internal animation and timer system will still run against real time.
- The sleep timeout check in `ui_manager_tick()` (`lv_tick_get() - s_last_activity_tick >= SLEEP_TIMEOUT_MS`) will behave correctly in real-time but incorrectly in fast-forward headless mode.

**Fix:** The spec must describe that in headless mode, `sim_display_init()` calls `lv_tick_set_cb(sim_get_simulated_tick)` where the simulated tick counter is incremented by `wait` events. All frame screenshots and LVGL timer evaluations must run against this counter.

---

### 5. The `--events` inline syntax cannot dismiss by ID — it can only dismiss by index, but `ui_manager` dismisses by ID

The `BLE_EVT_NOTIF_DISMISS` event carries an `id` field (a string), not an index. `notif_store_dismiss()` looks up notifications by ID. The inline event syntax `dismiss <index>` produces an integer index, which must be converted to an ID string before being handed to `ui_manager_handle_event()`.

The spec does not describe how the simulator maps a dismiss-by-index to the correct notification ID. The simulator would need to keep its own list of injected notification IDs in order to do this mapping.

**Fix:** Either:
- Change the inline syntax to `dismiss "<id>"` and require the same ID used in the prior `notify` command, OR
- Keep `dismiss <index>` but specify that `sim_events.c` maintains a parallel ordered list of injected IDs and maps index → ID at dispatch time. The spec must describe this bookkeeping explicitly.

---

### 6. No `--run-ms` default behavior defined when `--events` ends with a `wait`

The spec says `--run-ms` defaults to "inferred from events" but does not define what "inferred" means. If the last event is a `wait 2000`, does the simulation run 2000ms after that wait, or does it stop immediately at the end of the wait? For screenshot-based iteration, this matters because the agent needs to see the settled UI state after the last event.

**Fix:** Specify the inference rule: `--run-ms` defaults to `(time of last event) + (some grace period, e.g., 1000ms)`. State this explicitly so the implementer knows what to code and the agent knows what to expect in screenshots.

---

## SUGGESTION (nice to have)

### 7. `lv_conf.h` is missing required SDL sub-options

The spec lists `LV_USE_SDL 1` in `lv_conf.h` but omits the nested SDL options that the template requires when `LV_USE_SDL` is enabled:
- `LV_SDL_INCLUDE_PATH` — must be `<SDL2/SDL.h>` for Homebrew on macOS
- `LV_SDL_RENDER_MODE` — affects whether headless framebuffer capture works correctly; `LV_DISPLAY_RENDER_MODE_FULL` is the simplest for screenshot capture
- `LV_SDL_BUF_COUNT` — needs to be set
- `LV_SDL_DIRECT_EXIT` — should be `0` to prevent `exit(0)` on window close from terminating the process before the screenshot is written

The template guards all of these under `#if LV_USE_SDL`. Without specifying them, the implementer will use whatever defaults are in the template, which may not be correct for this use case.

---

### 8. `sim_display_get_framebuffer()` return type may not match the LVGL SDL SW driver's actual buffer

The spec declares `uint16_t *sim_display_get_framebuffer(void)`. The LVGL v9 SDL SW driver (`lv_sdl_sw.c`) stores its framebuffer as `uint8_t *fb1` and uses whatever color format is configured. With `LV_COLOR_DEPTH 16`, the buffer contains RGB565 pixels, so a `uint16_t *` cast works. But the screenshot code in `sim_screenshot.c` must know the stride (bytes per row), which for RGB565 with `LV_DISPLAY_RENDER_MODE_FULL` is `width * 2`. The spec does not mention stride anywhere in the screenshot API or implementation notes, which will confuse the implementer.

**Fix:** Document that the framebuffer is a flat array of `width * height` RGB565 pixels in row-major order, stride = `width * 2`, no padding.

---

### 9. Scene.c uses `__has_include` — this is Clang/GCC >= 5 only

`scene.c` uses `__has_include("assets/sprite_sleeping.h")`. This is supported by Clang and GCC 5+ (both fine on macOS) but is worth noting in the spec's build requirements section to make it explicit that GCC < 5 is not supported. Not a blocker on macOS, but worth one line.

---

### 10. No mention of `lv_init()` call sequence in `sim_main.c`

The spec describes what `sim_display.c` does but does not explicitly state the required initialization order for the simulator's `main()`:
1. `lv_init()`
2. `sim_display_init()` (creates SDL window / headless buffer, registers flush callback)
3. `lv_tick_set_cb()` (headless only — for simulated time)
4. `ui_manager_init()`
5. main loop

`ui_manager_init()` calls `lv_screen_active()`, which requires a display to be registered. If `sim_display_init()` is called after `ui_manager_init()`, it will crash. The spec should include a pseudocode initialization sequence in `sim_main.c`.

---

### 11. `cJSON` dependency is only needed for scenario file parsing — headless `--events` mode can work without it

If an implementer wants to skip cJSON for a first pass, only `--events` (inline parsing) is needed. The spec could note that cJSON is optional and only required for `--scenario`. This is minor but useful for a phased build-out.

---

## OK (no problems found)

- **LVGL v9.5 SDL2 driver compatibility:** Confirmed. `lv_sdl_window_create()`, `lv_sdl_window_set_zoom()`, `lv_sdl_quit()` all exist in the managed component at `firmware/managed_components/lvgl__lvgl/src/drivers/sdl/`. The driver handles its own SDL event loop via an LVGL timer, which means the simulator's main loop does not need to call `SDL_PollEvent` manually.

- **Shim completeness for the four compiled files:** Confirmed. None of `scene.c`, `notification_ui.c`, `notification.c`, or their headers include `freertos/` or `esp_log.h` directly. Only `ui_manager.c` includes `esp_log.h` (shim covers this) and uses `_lock_t` (covered by issue #1 above). The `ble_service.h` include chain is: `ui_manager.h` -> `ble_service.h` -> `freertos/FreeRTOS.h` + `freertos/queue.h`, all of which the shims cover once issue #1 is resolved.

- **`QueueHandle_t` and `pdMS_TO_TICKS` are not used in the four compiled files:** Confirmed. `QueueHandle_t` and `xQueueSend/xQueueReceive` appear only in `ble_service.c` and `main.c`, neither of which is compiled in the simulator. The FreeRTOS shim only needs to define `QueueHandle_t` and `TickType_t` to satisfy the `ble_service.h` type declarations that appear transitively through `ui_manager.h`.

- **`lv_malloc_zeroed`, `LV_IMAGE_HEADER_MAGIC`, `lv_timer_get_user_data`:** All confirmed present in LVGL v9.5 headers.

- **`lv_tick_set_cb()` exists:** Confirmed in `src/tick/lv_tick.h`. The simulated-time mechanism is implementable.

- **`lv_color_t` field names (.red, .green, .blue):** Confirmed. LVGL v9 defines `lv_color_t` with `uint8_t blue; uint8_t green; uint8_t red;` fields. `scene.c`'s `star_cfg` designated initializers will compile correctly.

- **Directory structure is reasonable:** The layout separates concerns cleanly. Vendored dependencies (cJSON, stb) are appropriate choices. Using the existing `firmware/managed_components/lvgl__lvgl/` avoids version drift.

- **`__has_include` guards in scene.c:** Sprites exist (`sprite_sleeping.h`, `sprite_disconnected.h` confirmed present), so both `HAS_SLEEPING_SPRITE` and `HAS_DISCONNECTED_SPRITE` will be 1. No fallback paths will be taken.

- **`lv_sdl_window_set_zoom()` supports the `--scale` flag:** Confirmed. `lv_sdl_window_set_zoom(disp, (float)scale)` is available and does exactly what the spec needs for the 3x default scale.

---

## Priority Order for Fixes

1. Issue #1 — `_lock_t` not defined on macOS (build breaks immediately)
2. Issue #3 — Double `lv_timer_handler()` call (silent timer misbehavior)
3. Issue #4 — Headless simulated tick not connected to `lv_tick_set_cb()` (fast-forward mode broken)
4. Issue #2 — `ble_service.h` shim uses hardcoded sizes (fragile, could corrupt silently)
5. Issue #5 — `dismiss <index>` has no ID mapping path (runtime crash or wrong behavior)
6. Issue #6 — `--run-ms` inference rule undefined (implementer ambiguity)
