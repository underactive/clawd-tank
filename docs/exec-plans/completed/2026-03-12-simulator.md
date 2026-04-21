# Clawd Simulator Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a native macOS simulator that runs the Clawd LVGL UI without hardware, supporting interactive (SDL2 window) and headless (PNG screenshots) modes for agent-driven development.

**Architecture:** The simulator is a separate CMake project in `simulator/` that compiles existing firmware rendering code (`scene.c`, `notification_ui.c`, `ui_manager.c`, `notification.c`) unmodified, using shim headers to replace ESP-IDF APIs. Display is handled via manual SDL2 (interactive) or plain framebuffer (headless), with `stb_image_write` for PNG screenshots. LVGL's built-in SDL driver is NOT used — we manage SDL2 directly for full keyboard event control.

**Tech Stack:** C, CMake, LVGL v9.5, SDL2 (Homebrew), stb_image_write, cJSON

---

## Chunk 1: Build Foundation

### Task 1: Create shim headers

ESP-IDF shim headers that shadow real ESP-IDF includes so firmware code compiles unmodified on macOS.

**Files:**
- Create: `simulator/shims/esp_log.h`
- Create: `simulator/shims/freertos/FreeRTOS.h`
- Create: `simulator/shims/freertos/queue.h`
- Create: `simulator/shims/freertos/task.h`
- Create: `simulator/shims/ble_service.h`

- [ ] **Step 1: Create `simulator/shims/esp_log.h`**

```c
#ifndef SIM_ESP_LOG_H
#define SIM_ESP_LOG_H

#include <stdio.h>

#define ESP_LOGI(tag, fmt, ...) printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) /* no-op */
#define ESP_LOGV(tag, fmt, ...) /* no-op */

#endif
```

- [ ] **Step 2: Create `simulator/shims/freertos/FreeRTOS.h`**

Must provide `_lock_t` stubs (ESP-IDF newlib type, not available on macOS) and FreeRTOS typedefs:

```c
#ifndef SIM_FREERTOS_H
#define SIM_FREERTOS_H

typedef void *QueueHandle_t;
typedef unsigned int TickType_t;
typedef int BaseType_t;

#define pdTRUE  1
#define pdFALSE 0
#define pdMS_TO_TICKS(ms) (ms)

/* ESP-IDF newlib _lock_t stubs — single-threaded simulator */
typedef int _lock_t;
#define _lock_init(lock)
#define _lock_acquire(lock)
#define _lock_release(lock)

#endif
```

- [ ] **Step 3: Create `simulator/shims/freertos/queue.h`**

```c
#ifndef SIM_FREERTOS_QUEUE_H
#define SIM_FREERTOS_QUEUE_H

#include "FreeRTOS.h"

/* Stub — simulator does not use FreeRTOS queues */
static inline QueueHandle_t xQueueCreate(int len, int size) { (void)len; (void)size; return NULL; }
static inline BaseType_t xQueueReceive(QueueHandle_t q, void *buf, TickType_t wait) { (void)q; (void)buf; (void)wait; return pdFALSE; }
static inline BaseType_t xQueueSend(QueueHandle_t q, const void *buf, TickType_t wait) { (void)q; (void)buf; (void)wait; return pdTRUE; }

#endif
```

- [ ] **Step 4: Create `simulator/shims/freertos/task.h`**

```c
#ifndef SIM_FREERTOS_TASK_H
#define SIM_FREERTOS_TASK_H

#include "FreeRTOS.h"

static inline void vTaskDelay(TickType_t ticks) { (void)ticks; }

#endif
```

- [ ] **Step 5: Create `simulator/shims/ble_service.h`**

Must mirror the real `firmware/main/ble_service.h` exactly — same enum values, same struct layout using notification.h constants:

```c
#ifndef SIM_BLE_SERVICE_H
#define SIM_BLE_SERVICE_H

#include "notification.h"

typedef enum {
    BLE_EVT_NOTIF_ADD,
    BLE_EVT_NOTIF_DISMISS,
    BLE_EVT_NOTIF_CLEAR,
    BLE_EVT_CONNECTED,
    BLE_EVT_DISCONNECTED,
} ble_evt_type_t;

typedef struct {
    ble_evt_type_t type;
    char id[NOTIF_MAX_ID_LEN];
    char project[NOTIF_MAX_PROJ_LEN];
    char message[NOTIF_MAX_MSG_LEN];
} ble_evt_t;

/* Stub — simulator does not init real BLE */
static inline void ble_service_init(void *q) { (void)q; }

#endif
```

- [ ] **Step 6: Commit**

```bash
git add simulator/shims/
git commit -m "feat(sim): add ESP-IDF shim headers for native macOS build"
```

---

### Task 2: Create LVGL configuration for simulator

**Files:**
- Create: `simulator/lv_conf.h`

- [ ] **Step 1: Create minimal `simulator/lv_conf.h`**

LVGL v9's `lv_conf_internal.h` uses `#ifndef` for every setting, so we only define what differs from defaults. The file must start with `#if 1` (the template uses `#if 0` as a guard).

```c
/**
 * LVGL configuration for Clawd simulator (native macOS build).
 * Only overrides that differ from defaults — see lv_conf_internal.h for all options.
 */
#if 1 /* Enable this config */

#ifndef LV_CONF_H
#define LV_CONF_H

/* Color depth matching firmware: 16-bit RGB565 */
#define LV_COLOR_DEPTH 16

/* Fonts used by the firmware UI */
#define LV_FONT_MONTSERRAT_8  1
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_18 1

/* Software renderer (no GPU) */
#define LV_USE_DRAW_SW 1

/* Disable LVGL's SDL driver — we manage SDL2 directly */
#define LV_USE_SDL 0

/* No OS integration */
#define LV_USE_OS LV_OS_NONE

/* Memory: use stdlib malloc (no custom allocator) */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN

/* Logging via our esp_log.h shim */
#define LV_USE_LOG 0

/* Disable demos and examples */
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_BENCHMARK 0

#endif /* LV_CONF_H */
#endif /* Enable config */
```

- [ ] **Step 2: Commit**

```bash
git add simulator/lv_conf.h
git commit -m "feat(sim): add LVGL lv_conf.h for native simulator build"
```

---

### Task 3: Create CMakeLists.txt and build smoke test

**Files:**
- Create: `simulator/CMakeLists.txt`
- Create: `simulator/sim_main.c` (minimal — just `lv_init` + print)

- [ ] **Step 1: Create `simulator/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.16)
project(clawd-sim LANGUAGES C)

set(CMAKE_C_STANDARD 11)

# SDL2 (from Homebrew)
find_package(SDL2 REQUIRED)

# Paths
set(FIRMWARE_DIR ${CMAKE_SOURCE_DIR}/../firmware/main)
set(LVGL_DIR ${CMAKE_SOURCE_DIR}/../firmware/managed_components/lvgl__lvgl)

# Build LVGL as a subdirectory — it detects non-ESP and uses os_desktop.cmake
# Point it to our lv_conf.h via LV_BUILD_CONF_DIR
set(LV_BUILD_CONF_DIR ${CMAKE_SOURCE_DIR} CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory(${LVGL_DIR} ${CMAKE_BINARY_DIR}/lvgl)

# Simulator executable
add_executable(clawd-sim
    sim_main.c
    # Shared firmware sources (compiled unmodified)
    ${FIRMWARE_DIR}/scene.c
    ${FIRMWARE_DIR}/notification_ui.c
    ${FIRMWARE_DIR}/ui_manager.c
    ${FIRMWARE_DIR}/notification.c
)

# Include path order matters:
# 1. shims/ first — shadows ESP-IDF headers
# 2. simulator/ — for lv_conf.h (already handled by LVGL's LV_BUILD_CONF_DIR)
# 3. LVGL headers — handled by linking lvgl target
# 4. firmware/main/ — for scene.h, notification.h, etc.
target_include_directories(clawd-sim PRIVATE
    ${CMAKE_SOURCE_DIR}/shims    # ESP-IDF shims (must be first)
    ${CMAKE_SOURCE_DIR}          # lv_conf.h lives here
    ${FIRMWARE_DIR}              # firmware headers (scene.h, notification.h, etc.)
    ${FIRMWARE_DIR}/assets       # sprite asset headers
)

target_link_libraries(clawd-sim PRIVATE
    lvgl
    SDL2::SDL2
)

# macOS: SDL2 needs Cocoa framework
if(APPLE)
    target_link_libraries(clawd-sim PRIVATE "-framework Cocoa")
endif()
```

- [ ] **Step 2: Create minimal `simulator/sim_main.c`**

```c
#include "lvgl.h"
#include <stdio.h>

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    lv_init();
    printf("LVGL initialized. Simulator build works!\n");
    return 0;
}
```

- [ ] **Step 3: Build and verify compilation succeeds**

Run:
```bash
cd simulator && cmake -B build && cmake --build build
```

Expected: Build succeeds, `clawd-sim` binary is created. If there are compile errors from the shared firmware code (missing includes, type conflicts), fix the shim headers.

- [ ] **Step 4: Run the binary to verify LVGL init works**

Run: `./simulator/build/clawd-sim`

Expected output: `LVGL initialized. Simulator build works!`

- [ ] **Step 5: Commit**

```bash
git add simulator/CMakeLists.txt simulator/sim_main.c
git commit -m "feat(sim): CMake build + smoke test — LVGL compiles natively"
```

---

## Chunk 2: Headless Display + Screenshots

### Task 4: Implement sim_display — headless framebuffer

**Files:**
- Create: `simulator/sim_display.h`
- Create: `simulator/sim_display.c`
- Modify: `simulator/sim_main.c`
- Modify: `simulator/CMakeLists.txt` — add sim_display.c

- [ ] **Step 1: Create `simulator/sim_display.h`**

```c
#ifndef SIM_DISPLAY_H
#define SIM_DISPLAY_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

#define SIM_LCD_H_RES 320
#define SIM_LCD_V_RES 172

/**
 * Initialize the display.
 * @param headless  If true, no SDL window (framebuffer only).
 * @param scale     Window scale factor (interactive mode only; ignored if headless).
 * @return The LVGL display object.
 */
lv_display_t *sim_display_init(bool headless, int scale);

/** Get pointer to the raw RGB565 framebuffer (320*172 uint16_t). */
uint16_t *sim_display_get_framebuffer(void);

/** Pump SDL events (interactive) or no-op (headless). */
void sim_display_tick(void);

/** Returns true if user closed the SDL window. */
bool sim_display_should_quit(void);

/** Clean up SDL resources. */
void sim_display_shutdown(void);

/** Signal that the window should close. */
void sim_display_set_quit(void);

/* Simulated time for headless mode */
uint32_t sim_get_tick(void);
void sim_advance_tick(uint32_t ms);

#endif
```

- [ ] **Step 2: Create `simulator/sim_display.c` — headless mode only first**

```c
#include "sim_display.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <SDL2/SDL.h>

/* Framebuffer — always maintained, both modes read from this */
static uint16_t s_framebuffer[SIM_LCD_H_RES * SIM_LCD_V_RES];

/* Mode flag */
static bool s_headless = false;
static bool s_quit = false;

/* Simulated tick for headless mode */
static uint32_t s_sim_tick = 0;

/* SDL state (interactive mode) */
static SDL_Window   *s_window   = NULL;
static SDL_Renderer *s_renderer = NULL;
static SDL_Texture  *s_texture  = NULL;
static int s_scale = 3;

/* ---- Simulated time ---- */

uint32_t sim_get_tick(void)
{
    return s_sim_tick;
}

void sim_advance_tick(uint32_t ms)
{
    s_sim_tick += ms;
}

static uint32_t sdl_tick_cb(void)
{
    return SDL_GetTicks();
}

/* ---- LVGL flush callback ---- */

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    int x1 = area->x1;
    int y1 = area->y1;
    int w  = area->x2 - area->x1 + 1;
    int h  = area->y2 - area->y1 + 1;

    uint16_t *src = (uint16_t *)px_map;
    for (int y = 0; y < h; y++) {
        memcpy(&s_framebuffer[(y1 + y) * SIM_LCD_H_RES + x1],
               &src[y * w],
               w * sizeof(uint16_t));
    }

    lv_display_flush_ready(disp);
}

/* ---- Init ---- */

lv_display_t *sim_display_init(bool headless, int scale)
{
    s_headless = headless;
    s_scale = scale > 0 ? scale : 3;
    memset(s_framebuffer, 0, sizeof(s_framebuffer));

    /* Set LVGL tick source */
    if (headless) {
        lv_tick_set_cb(sim_get_tick);
    } else {
        /* Interactive: use SDL_GetTicks */
        SDL_Init(SDL_INIT_VIDEO);
        lv_tick_set_cb(sdl_tick_cb);

        s_window = SDL_CreateWindow(
            "Clawd Simulator",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            SIM_LCD_H_RES * s_scale, SIM_LCD_V_RES * s_scale,
            0);
        if (!s_window) {
            fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
            exit(1);
        }

        s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!s_renderer) {
            fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
            exit(1);
        }

        /* Nearest-neighbor scaling for crisp pixels */
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

        s_texture = SDL_CreateTexture(
            s_renderer,
            SDL_PIXELFORMAT_RGB565,
            SDL_TEXTUREACCESS_STREAMING,
            SIM_LCD_H_RES, SIM_LCD_V_RES);
        if (!s_texture) {
            fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
            exit(1);
        }
    }

    /* Create LVGL display */
    lv_display_t *disp = lv_display_create(SIM_LCD_H_RES, SIM_LCD_V_RES);

    /* Allocate render buffers */
    size_t buf_sz = SIM_LCD_H_RES * 20 * sizeof(uint16_t); /* 20-line partial buffer */
    void *buf1 = malloc(buf_sz);
    void *buf2 = malloc(buf_sz);

    lv_display_set_buffers(disp, buf1, buf2, buf_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, flush_cb);

    return disp;
}

uint16_t *sim_display_get_framebuffer(void)
{
    return s_framebuffer;
}

/* ---- Tick ---- */

void sim_display_tick(void)
{
    if (s_headless) return;

    /* Present framebuffer to SDL window */
    SDL_UpdateTexture(s_texture, NULL, s_framebuffer, SIM_LCD_H_RES * sizeof(uint16_t));
    SDL_RenderClear(s_renderer);
    SDL_RenderCopy(s_renderer, s_texture, NULL, NULL); /* SDL handles upscale */
    SDL_RenderPresent(s_renderer);
}

bool sim_display_should_quit(void)
{
    return s_quit;
}

void sim_display_set_quit(void)
{
    s_quit = true;
}

/* ---- Shutdown ---- */

void sim_display_shutdown(void)
{
    if (!s_headless) {
        if (s_texture)  SDL_DestroyTexture(s_texture);
        if (s_renderer) SDL_DestroyRenderer(s_renderer);
        if (s_window)   SDL_DestroyWindow(s_window);
        SDL_Quit();
    }
}
```

- [ ] **Step 3: Add `sim_display.c` to CMakeLists.txt**

In `simulator/CMakeLists.txt`, add `sim_display.c` to the `add_executable` source list.

- [ ] **Step 4: Update `sim_main.c` to init the display and render one frame**

```c
#include "lvgl.h"
#include "sim_display.h"
#include "ui_manager.h"
#include <stdio.h>

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    lv_init();
    sim_display_init(true, 3); /* headless for now */
    ui_manager_init();

    /* Run a few ticks to let LVGL render */
    for (int i = 0; i < 10; i++) {
        sim_advance_tick(33);
        ui_manager_tick();
    }

    printf("Simulator rendered %d frames. Framebuffer at %p\n",
           10, (void *)sim_display_get_framebuffer());
    return 0;
}
```

- [ ] **Step 5: Build and run**

Run: `cd simulator && cmake --build build && ./build/clawd-sim`

Expected: Compiles and prints the frame count message. No crash means LVGL + scene + notification_ui all initialized correctly.

- [ ] **Step 6: Commit**

```bash
git add simulator/sim_display.h simulator/sim_display.c simulator/sim_main.c simulator/CMakeLists.txt
git commit -m "feat(sim): display backend with headless framebuffer + SDL2 window"
```

---

### Task 5: Implement screenshot capture

**Files:**
- Create: `simulator/sim_screenshot.h`
- Create: `simulator/sim_screenshot.c`
- Download: `simulator/stb_image_write.h`
- Modify: `simulator/CMakeLists.txt` — add sim_screenshot.c
- Modify: `simulator/sim_main.c` — capture a test screenshot

- [ ] **Step 1: Download `stb_image_write.h`**

```bash
curl -sL https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h -o simulator/stb_image_write.h
```

- [ ] **Step 2: Create `simulator/sim_screenshot.h`**

```c
#ifndef SIM_SCREENSHOT_H
#define SIM_SCREENSHOT_H

#include <stdint.h>

/** Initialize screenshot system. Creates output_dir if it doesn't exist. */
void sim_screenshot_init(const char *output_dir);

/**
 * Capture a screenshot from the RGB565 framebuffer.
 * @param framebuffer  Pointer to 320*172 uint16_t array
 * @param w            Width (320)
 * @param h            Height (172)
 * @param time_ms      Simulated time in ms (used for filename)
 * @param suffix       Optional suffix for filename (e.g. "connect"), or NULL
 */
void sim_screenshot_capture(const uint16_t *framebuffer, int w, int h,
                            uint32_t time_ms, const char *suffix);

#endif
```

- [ ] **Step 3: Create `simulator/sim_screenshot.c`**

```c
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "sim_screenshot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char s_output_dir[256] = "";

void sim_screenshot_init(const char *output_dir)
{
    if (!output_dir) return;
    snprintf(s_output_dir, sizeof(s_output_dir), "%s", output_dir);

    /* Create directory if it doesn't exist */
    mkdir(s_output_dir, 0755);
}

void sim_screenshot_capture(const uint16_t *framebuffer, int w, int h,
                            uint32_t time_ms, const char *suffix)
{
    if (!s_output_dir[0]) return;

    /* Build filename */
    char path[512];
    if (suffix && suffix[0]) {
        snprintf(path, sizeof(path), "%s/event_%06u_%s.png",
                 s_output_dir, time_ms, suffix);
    } else {
        snprintf(path, sizeof(path), "%s/frame_%06u.png",
                 s_output_dir, time_ms);
    }

    /* Convert RGB565 to RGB888 */
    uint8_t *rgb = malloc(w * h * 3);
    if (!rgb) return;

    for (int i = 0; i < w * h; i++) {
        uint16_t pixel = framebuffer[i];
        uint8_t r5 = (pixel >> 11) & 0x1F;
        uint8_t g6 = (pixel >> 5) & 0x3F;
        uint8_t b5 = pixel & 0x1F;
        rgb[i * 3 + 0] = (r5 << 3) | (r5 >> 2);
        rgb[i * 3 + 1] = (g6 << 2) | (g6 >> 4);
        rgb[i * 3 + 2] = (b5 << 3) | (b5 >> 2);
    }

    stbi_write_png(path, w, h, 3, rgb, w * 3);
    free(rgb);

    printf("[screenshot] %s\n", path);
}
```

- [ ] **Step 4: Add to CMakeLists.txt and update sim_main.c**

Add `sim_screenshot.c` to `add_executable` sources. Update `sim_main.c` to capture a test screenshot after rendering:

```c
#include "sim_screenshot.h"
// ... after the render loop:
sim_screenshot_init("./shots");
sim_screenshot_capture(sim_display_get_framebuffer(),
                       SIM_LCD_H_RES, SIM_LCD_V_RES, 0, NULL);
printf("Screenshot saved to ./shots/frame_000000.png\n");
```

- [ ] **Step 5: Build, run, verify PNG is created**

Run:
```bash
cd simulator && cmake --build build && ./build/clawd-sim
ls -la shots/frame_000000.png
```

Expected: PNG file exists. Open it to verify it shows the Clawd disconnected state (dark sky, sprite, BLE icon, "No connection" label).

- [ ] **Step 6: Commit**

```bash
git add simulator/stb_image_write.h simulator/sim_screenshot.h simulator/sim_screenshot.c simulator/sim_main.c simulator/CMakeLists.txt
git commit -m "feat(sim): PNG screenshot capture from RGB565 framebuffer"
```

---

## Chunk 3: Event Injection + Full Headless Mode

### Task 6: Implement event injection (inline events)

**Files:**
- Create: `simulator/sim_events.h`
- Create: `simulator/sim_events.c`
- Modify: `simulator/CMakeLists.txt` — add sim_events.c

- [ ] **Step 1: Create `simulator/sim_events.h`**

```c
#ifndef SIM_EVENTS_H
#define SIM_EVENTS_H

#include <stdbool.h>
#include <stdint.h>

/** Initialize event system from an inline event string (semicolon-separated). */
void sim_events_init_inline(const char *events_str);

/** Initialize event system from a JSON scenario file. */
void sim_events_init_scenario(const char *path);

/**
 * Process any events that are due at the given simulated time.
 * Fires events via ui_manager_handle_event().
 * @return true if an event was fired (caller should capture screenshot if --screenshot-on-event).
 */
bool sim_events_process(uint32_t current_time_ms);

/** Returns true when all events have been processed. */
bool sim_events_all_done(void);

/** Get the time of the last event (for --run-ms default calculation). */
uint32_t sim_events_get_end_time(void);

/** Get the suffix string for the most recently fired event (for screenshot naming). */
const char *sim_events_last_suffix(void);

#endif
```

- [ ] **Step 2: Create `simulator/sim_events.c`**

Parses the inline `--events` string into a timed event queue. Supports: `connect`, `disconnect`, `notify "project" "message"`, `dismiss <index>`, `clear`, `wait <ms>`.

```c
#include "sim_events.h"
#include "ble_service.h"
#include "ui_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_EVENTS 64
#define MAX_NOTIF_IDS 16

typedef struct {
    uint32_t time_ms;       /* absolute simulated time */
    ble_evt_t evt;          /* event to fire */
    bool is_wait;           /* true = just a time advance, no event */
    char suffix[32];        /* for screenshot naming */
} sim_event_t;

static sim_event_t s_events[MAX_EVENTS];
static int s_event_count = 0;
static int s_event_cursor = 0;  /* next event to process */
static char s_last_suffix[32] = "";

/* Track injected notification IDs for dismiss-by-index */
static char s_notif_ids[MAX_NOTIF_IDS][NOTIF_MAX_ID_LEN];
static int s_notif_id_count = 0;
static int s_next_id = 1;

static void add_event(uint32_t time_ms, const ble_evt_t *evt, const char *suffix)
{
    if (s_event_count >= MAX_EVENTS) return;
    sim_event_t *e = &s_events[s_event_count++];
    e->time_ms = time_ms;
    e->is_wait = false;
    if (evt) {
        e->evt = *evt;
    }
    if (suffix) {
        snprintf(e->suffix, sizeof(e->suffix), "%s", suffix);
    } else {
        e->suffix[0] = '\0';
    }
}

/* Skip whitespace */
static const char *skip_ws(const char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

/* Parse a quoted string, returns pointer past closing quote */
static const char *parse_quoted(const char *s, char *out, int out_sz)
{
    s = skip_ws(s);
    if (*s != '"') {
        /* Unquoted — read until space or semicolon */
        int i = 0;
        while (*s && *s != ';' && *s != ' ' && i < out_sz - 1) {
            out[i++] = *s++;
        }
        out[i] = '\0';
        return s;
    }
    s++; /* skip opening quote */
    int i = 0;
    while (*s && *s != '"' && i < out_sz - 1) {
        out[i++] = *s++;
    }
    out[i] = '\0';
    if (*s == '"') s++; /* skip closing quote */
    return s;
}

void sim_events_init_inline(const char *events_str)
{
    if (!events_str) return;

    uint32_t current_time = 0;
    const char *p = events_str;

    while (*p) {
        p = skip_ws(p);
        if (!*p) break;

        if (strncmp(p, "connect", 7) == 0 && (!p[7] || p[7] == ';' || isspace((unsigned char)p[7]))) {
            ble_evt_t evt = { .type = BLE_EVT_CONNECTED };
            add_event(current_time, &evt, "connect");
            p += 7;
        }
        else if (strncmp(p, "disconnect", 10) == 0 && (!p[10] || p[10] == ';' || isspace((unsigned char)p[10]))) {
            ble_evt_t evt = { .type = BLE_EVT_DISCONNECTED };
            add_event(current_time, &evt, "disconnect");
            p += 10;
        }
        else if (strncmp(p, "clear", 5) == 0 && (!p[5] || p[5] == ';' || isspace((unsigned char)p[5]))) {
            ble_evt_t evt = { .type = BLE_EVT_NOTIF_CLEAR };
            add_event(current_time, &evt, "clear");
            p += 5;
        }
        else if (strncmp(p, "notify", 6) == 0) {
            p += 6;
            ble_evt_t evt = { .type = BLE_EVT_NOTIF_ADD };
            /* Generate a unique ID */
            snprintf(evt.id, sizeof(evt.id), "sim_%d", s_next_id);
            /* Track ID for dismiss-by-index */
            if (s_notif_id_count < MAX_NOTIF_IDS) {
                strncpy(s_notif_ids[s_notif_id_count], evt.id, NOTIF_MAX_ID_LEN - 1);
                s_notif_id_count++;
            }
            s_next_id++;
            p = parse_quoted(p, evt.project, sizeof(evt.project));
            p = parse_quoted(p, evt.message, sizeof(evt.message));
            add_event(current_time, &evt, "notify");
        }
        else if (strncmp(p, "dismiss", 7) == 0) {
            p += 7;
            p = skip_ws(p);
            int index = atoi(p);
            while (*p && *p != ';' && !isspace((unsigned char)*p)) p++;

            ble_evt_t evt = { .type = BLE_EVT_NOTIF_DISMISS };
            if (index >= 0 && index < s_notif_id_count) {
                strncpy(evt.id, s_notif_ids[index], sizeof(evt.id) - 1);
            }
            add_event(current_time, &evt, "dismiss");
        }
        else if (strncmp(p, "wait", 4) == 0) {
            p += 4;
            p = skip_ws(p);
            uint32_t ms = (uint32_t)atoi(p);
            while (*p && *p != ';' && !isspace((unsigned char)*p)) p++;
            current_time += ms;
        }
        else {
            /* Unknown token — skip to next semicolon */
            while (*p && *p != ';') p++;
        }

        /* Skip to next command */
        p = skip_ws(p);
        if (*p == ';') p++;
    }
}

void sim_events_init_scenario(const char *path)
{
    /* TODO: Task 9 — JSON scenario parsing with cJSON */
    (void)path;
    fprintf(stderr, "Scenario files not yet implemented\n");
}

bool sim_events_process(uint32_t current_time_ms)
{
    bool fired = false;

    while (s_event_cursor < s_event_count &&
           s_events[s_event_cursor].time_ms <= current_time_ms) {

        sim_event_t *e = &s_events[s_event_cursor];
        s_event_cursor++;

        if (!e->is_wait) {
            ui_manager_handle_event(&e->evt);
            snprintf(s_last_suffix, sizeof(s_last_suffix), "%s", e->suffix);
            fired = true;
        }
    }

    return fired;
}

bool sim_events_all_done(void)
{
    return s_event_cursor >= s_event_count;
}

uint32_t sim_events_get_end_time(void)
{
    if (s_event_count == 0) return 0;
    return s_events[s_event_count - 1].time_ms;
}

const char *sim_events_last_suffix(void)
{
    return s_last_suffix;
}
```

- [ ] **Step 3: Add `sim_events.c` to CMakeLists.txt**

- [ ] **Step 4: Build to verify compilation**

Run: `cd simulator && cmake --build build`

Expected: Compiles with no errors.

- [ ] **Step 5: Commit**

```bash
git add simulator/sim_events.h simulator/sim_events.c simulator/CMakeLists.txt
git commit -m "feat(sim): event injection — parse inline --events strings"
```

---

### Task 7: Full headless main loop with CLI

**Files:**
- Modify: `simulator/sim_main.c` — full CLI, headless main loop

- [ ] **Step 1: Rewrite `simulator/sim_main.c` with full CLI argument parsing and headless loop**

```c
#include "lvgl.h"
#include "sim_display.h"
#include "sim_events.h"
#include "sim_screenshot.h"
#include "ui_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#define TICK_MS 33  /* ~30 fps */

/* ---- CLI options ---- */
static bool     opt_headless = false;
static int      opt_scale = 3;
static const char *opt_events = NULL;
static const char *opt_scenario = NULL;
static const char *opt_screenshot_dir = NULL;
static int      opt_screenshot_interval = 0;
static bool     opt_screenshot_on_event = false;
static uint32_t opt_run_ms = 0;

static void print_usage(void)
{
    printf(
        "Usage: clawd-sim [OPTIONS]\n"
        "\n"
        "Display:\n"
        "  --headless              Run without SDL2 window\n"
        "  --scale <N>             Window scale factor (default: 3)\n"
        "\n"
        "Events:\n"
        "  --events '<commands>'   Inline event string (semicolon-separated)\n"
        "  --scenario <file.json>  Load timed events from JSON file\n"
        "\n"
        "Screenshots:\n"
        "  --screenshot-dir <dir>  Output directory for PNGs\n"
        "  --screenshot-interval <ms>  Periodic screenshot interval\n"
        "  --screenshot-on-event   Screenshot after each event\n"
        "\n"
        "General:\n"
        "  --run-ms <ms>           Simulation duration (headless)\n"
        "  --help                  Show this help\n"
    );
}

static void parse_args(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--headless") == 0) {
            opt_headless = true;
        } else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            opt_scale = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--events") == 0 && i + 1 < argc) {
            opt_events = argv[++i];
        } else if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            opt_scenario = argv[++i];
        } else if (strcmp(argv[i], "--screenshot-dir") == 0 && i + 1 < argc) {
            opt_screenshot_dir = argv[++i];
        } else if (strcmp(argv[i], "--screenshot-interval") == 0 && i + 1 < argc) {
            opt_screenshot_interval = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--screenshot-on-event") == 0) {
            opt_screenshot_on_event = true;
        } else if (strcmp(argv[i], "--run-ms") == 0 && i + 1 < argc) {
            opt_run_ms = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage();
            exit(0);
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage();
            exit(1);
        }
    }
}

/* ---- Screenshot helpers ---- */

static uint32_t s_last_screenshot_time = 0;

static void maybe_capture_periodic(uint32_t time_ms)
{
    if (!opt_screenshot_dir || opt_screenshot_interval <= 0) return;

    while (s_last_screenshot_time <= time_ms) {
        sim_screenshot_capture(sim_display_get_framebuffer(),
                               SIM_LCD_H_RES, SIM_LCD_V_RES,
                               s_last_screenshot_time, NULL);
        s_last_screenshot_time += (uint32_t)opt_screenshot_interval;
    }
}

static void capture_on_event(uint32_t time_ms, const char *suffix)
{
    if (!opt_screenshot_dir || !opt_screenshot_on_event) return;
    sim_screenshot_capture(sim_display_get_framebuffer(),
                           SIM_LCD_H_RES, SIM_LCD_V_RES,
                           time_ms, suffix);
}

/* ---- Headless main loop ---- */

static void run_headless(void)
{
    uint32_t end_time = opt_run_ms;
    if (end_time == 0) {
        end_time = sim_events_get_end_time() + 500;
    }

    printf("[sim] Headless mode: running %u ms of simulated time\n", end_time);

    uint32_t time = 0;

    /* Initial render */
    ui_manager_tick();
    maybe_capture_periodic(time);

    while (time < end_time) {
        time += TICK_MS;
        sim_advance_tick(TICK_MS);

        /* Fire any due events */
        bool event_fired = sim_events_process(time);

        /* LVGL tick */
        ui_manager_tick();

        /* Screenshots */
        if (event_fired) {
            capture_on_event(time, sim_events_last_suffix());
        }
        maybe_capture_periodic(time);
    }

    printf("[sim] Done. Processed %u ms.\n", time);
}

/* ---- Interactive main loop ---- */

/* Forward declaration — keyboard handler defined below */
static void handle_sdl_events(void);

static void run_interactive(void)
{
    printf("[sim] Interactive mode (scale=%dx). Keys: c=connect d=disconnect n=notify x=clear s=screenshot q=quit\n",
           opt_scale);

    while (!sim_display_should_quit()) {
        handle_sdl_events();

        /* Process scripted events if any (using wall time) */
        if (opt_events || opt_scenario) {
            sim_events_process(SDL_GetTicks());
        }

        ui_manager_tick();
        sim_display_tick();  /* present to SDL window */

        SDL_Delay(TICK_MS);
    }
}

/* ---- Keyboard event handling (interactive) ---- */

static int s_sample_notif_idx = 0;
static const struct { const char *project; const char *message; } sample_notifs[] = {
    {"GitHub",  "PR #42: Fix auth flow"},
    {"Slack",   "Meeting in 5 minutes"},
    {"Jira",    "PROJ-123 moved to Done"},
    {"Email",   "New message from Alice"},
    {"CI/CD",   "Build #789 passed"},
    {"Discord", "New message in #general"},
};
#define SAMPLE_COUNT (sizeof(sample_notifs) / sizeof(sample_notifs[0]))

static void handle_sdl_events(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            sim_display_set_quit();
            return;
        }
        if (e.type == SDL_KEYDOWN) {
            switch (e.key.keysym.sym) {
            case SDLK_q:
            case SDLK_ESCAPE:
                sim_display_set_quit();
                return;

            case SDLK_c: {
                ble_evt_t evt = { .type = BLE_EVT_CONNECTED };
                ui_manager_handle_event(&evt);
                printf("[key] connect\n");
                break;
            }
            case SDLK_d: {
                ble_evt_t evt = { .type = BLE_EVT_DISCONNECTED };
                ui_manager_handle_event(&evt);
                printf("[key] disconnect\n");
                break;
            }
            case SDLK_n: {
                ble_evt_t evt = { .type = BLE_EVT_NOTIF_ADD };
                const char *proj = sample_notifs[s_sample_notif_idx % SAMPLE_COUNT].project;
                const char *msg  = sample_notifs[s_sample_notif_idx % SAMPLE_COUNT].message;
                snprintf(evt.id, sizeof(evt.id), "key_%d", s_sample_notif_idx);
                snprintf(evt.project, sizeof(evt.project), "%s", proj);
                snprintf(evt.message, sizeof(evt.message), "%s", msg);
                s_sample_notif_idx++;
                ui_manager_handle_event(&evt);
                printf("[key] notify: %s — %s\n", proj, msg);
                break;
            }
            case SDLK_x: {
                ble_evt_t evt = { .type = BLE_EVT_NOTIF_CLEAR };
                ui_manager_handle_event(&evt);
                printf("[key] clear all\n");
                break;
            }
            case SDLK_s: {
                sim_screenshot_init(".");
                sim_screenshot_capture(sim_display_get_framebuffer(),
                                       SIM_LCD_H_RES, SIM_LCD_V_RES,
                                       SDL_GetTicks(), "manual");
                printf("[key] screenshot saved\n");
                break;
            }
            case SDLK_1: case SDLK_2: case SDLK_3: case SDLK_4:
            case SDLK_5: case SDLK_6: case SDLK_7: case SDLK_8: {
                /* Dismiss by index — requires tracking, simplified for interactive */
                int idx = e.key.keysym.sym - SDLK_1;
                ble_evt_t evt = { .type = BLE_EVT_NOTIF_DISMISS };
                snprintf(evt.id, sizeof(evt.id), "key_%d", idx);
                ui_manager_handle_event(&evt);
                printf("[key] dismiss %d\n", idx);
                break;
            }
            default:
                break;
            }
        }
    }
}

/* ---- Main ---- */

int main(int argc, char *argv[])
{
    parse_args(argc, argv);

    /* 1. Init LVGL */
    lv_init();

    /* 2. Init display */
    sim_display_init(opt_headless, opt_scale);

    /* 3. Init events */
    if (opt_events) sim_events_init_inline(opt_events);
    if (opt_scenario) sim_events_init_scenario(opt_scenario);

    /* 4. Init UI manager (creates scene + notification panel) */
    ui_manager_init();

    /* 5. Init screenshots */
    if (opt_screenshot_dir) sim_screenshot_init(opt_screenshot_dir);

    /* 6. Run */
    if (opt_headless) {
        run_headless();
    } else {
        run_interactive();
    }

    /* 7. Cleanup */
    sim_display_shutdown();

    return 0;
}
```

- [ ] **Step 2: Build**

Run: `cd simulator && cmake --build build`

Expected: Compiles with no errors.

- [ ] **Step 3: Test headless mode with screenshots**

Run:
```bash
rm -rf shots && ./simulator/build/clawd-sim --headless \
  --events 'connect; wait 500; notify "GitHub" "PR merged"; wait 2000; disconnect' \
  --screenshot-interval 100 --screenshot-on-event \
  --screenshot-dir ./shots/
```

Expected: `shots/` directory contains multiple PNGs. Check that:
- `frame_000000.png` shows disconnected state
- An event screenshot around 000033 shows connected state
- A notification screenshot appears after connect
- Later frames show the notification panel

- [ ] **Step 4: Test interactive mode**

Run: `./simulator/build/clawd-sim`

Expected: A 960x516 SDL window appears showing the Clawd disconnected state. Press `c` to connect, `n` to add notifications, `d` to disconnect. Press `q` to quit.

- [ ] **Step 5: Commit**

```bash
git add simulator/sim_main.c simulator/sim_display.h
git commit -m "feat(sim): full CLI with headless + interactive modes, keyboard shortcuts"
```

---

## Chunk 4: Scenario JSON + Polish

### Task 8: Scenario JSON support

**Files:**
- Download: `simulator/cjson/cJSON.c` and `simulator/cjson/cJSON.h`
- Modify: `simulator/sim_events.c` — implement `sim_events_init_scenario()`
- Create: `simulator/scenarios/demo.json`
- Modify: `simulator/CMakeLists.txt` — add cJSON source

- [ ] **Step 1: Download cJSON**

```bash
mkdir -p simulator/cjson
curl -sL https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.c -o simulator/cjson/cJSON.c
curl -sL https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.h -o simulator/cjson/cJSON.h
```

- [ ] **Step 2: Add cJSON to CMakeLists.txt**

Add `cjson/cJSON.c` to `add_executable` sources. Add `${CMAKE_SOURCE_DIR}/cjson` to `target_include_directories`.

- [ ] **Step 3: Implement `sim_events_init_scenario()` in `sim_events.c`**

Add to top of file:
```c
#include "cJSON.h"
```

Replace the stub implementation with:

```c
void sim_events_init_scenario(const char *path)
{
    if (!path) return;

    /* Read file */
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open scenario file: %s\n", path);
        return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *json = malloc(sz + 1);
    fread(json, 1, sz, f);
    json[sz] = '\0';
    fclose(f);

    /* Parse JSON array */
    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!root || !cJSON_IsArray(root)) {
        fprintf(stderr, "Invalid scenario JSON\n");
        if (root) cJSON_Delete(root);
        return;
    }

    cJSON *item;
    cJSON_ArrayForEach(item, root) {
        uint32_t time_ms = 0;
        cJSON *t = cJSON_GetObjectItem(item, "time_ms");
        if (t && cJSON_IsNumber(t)) time_ms = (uint32_t)t->valuedouble;

        cJSON *ev = cJSON_GetObjectItem(item, "event");
        if (!ev || !cJSON_IsString(ev)) continue;

        const char *event_name = ev->valuestring;

        if (strcmp(event_name, "connect") == 0) {
            ble_evt_t evt = { .type = BLE_EVT_CONNECTED };
            add_event(time_ms, &evt, "connect");
        }
        else if (strcmp(event_name, "disconnect") == 0) {
            ble_evt_t evt = { .type = BLE_EVT_DISCONNECTED };
            add_event(time_ms, &evt, "disconnect");
        }
        else if (strcmp(event_name, "clear") == 0) {
            ble_evt_t evt = { .type = BLE_EVT_NOTIF_CLEAR };
            add_event(time_ms, &evt, "clear");
        }
        else if (strcmp(event_name, "notify") == 0) {
            ble_evt_t evt = { .type = BLE_EVT_NOTIF_ADD };
            snprintf(evt.id, sizeof(evt.id), "scn_%d", s_next_id);
            if (s_notif_id_count < MAX_NOTIF_IDS) {
                strncpy(s_notif_ids[s_notif_id_count], evt.id, NOTIF_MAX_ID_LEN - 1);
                s_notif_id_count++;
            }
            s_next_id++;

            cJSON *proj = cJSON_GetObjectItem(item, "project");
            cJSON *msg  = cJSON_GetObjectItem(item, "message");
            if (proj && cJSON_IsString(proj))
                snprintf(evt.project, sizeof(evt.project), "%s", proj->valuestring);
            if (msg && cJSON_IsString(msg))
                snprintf(evt.message, sizeof(evt.message), "%s", msg->valuestring);

            add_event(time_ms, &evt, "notify");
        }
        else if (strcmp(event_name, "dismiss") == 0) {
            ble_evt_t evt = { .type = BLE_EVT_NOTIF_DISMISS };
            cJSON *idx = cJSON_GetObjectItem(item, "index");
            if (idx && cJSON_IsNumber(idx)) {
                int index = (int)idx->valuedouble;
                if (index >= 0 && index < s_notif_id_count) {
                    strncpy(evt.id, s_notif_ids[index], sizeof(evt.id) - 1);
                }
            }
            add_event(time_ms, &evt, "dismiss");
        }
    }

    cJSON_Delete(root);
    printf("[sim] Loaded %d events from %s\n", s_event_count, path);
}
```

- [ ] **Step 4: Create `simulator/scenarios/demo.json`**

```json
[
    {"time_ms": 0,    "event": "connect"},
    {"time_ms": 500,  "event": "notify", "project": "GitHub", "message": "PR #42: Fix auth flow merged"},
    {"time_ms": 1500, "event": "notify", "project": "Slack",  "message": "Deploy to staging complete"},
    {"time_ms": 3000, "event": "notify", "project": "Jira",   "message": "PROJ-123 moved to Done"},
    {"time_ms": 5000, "event": "dismiss", "index": 0},
    {"time_ms": 7000, "event": "clear"},
    {"time_ms": 8000, "event": "disconnect"}
]
```

- [ ] **Step 5: Build and test with scenario file**

Run:
```bash
cd simulator && cmake -B build && cmake --build build
rm -rf shots && ./build/clawd-sim --headless \
  --scenario scenarios/demo.json \
  --screenshot-interval 500 --screenshot-on-event \
  --screenshot-dir ./shots/
ls shots/ | head -20
```

Expected: Multiple PNGs showing the progression: disconnected → connected → notifications → dismiss → clear → disconnected.

- [ ] **Step 6: Commit**

```bash
git add simulator/cjson/ simulator/sim_events.c simulator/CMakeLists.txt simulator/scenarios/demo.json
git commit -m "feat(sim): scenario JSON support with cJSON + demo scenario"
```

---

### Task 9: End-to-end verification and polish

**Files:**
- Modify: `simulator/sim_main.c` — any final fixes
- Create: `simulator/.gitignore`

- [ ] **Step 1: Create `simulator/.gitignore`**

```
build/
shots/
*.png
```

- [ ] **Step 2: Full end-to-end test — inline events**

```bash
rm -rf shots && ./simulator/build/clawd-sim --headless \
  --events 'connect; wait 200; notify "GitHub" "PR merged"; wait 1000; notify "Slack" "Deploy done"; wait 3000; disconnect' \
  --screenshot-interval 100 --screenshot-on-event \
  --screenshot-dir ./shots/
```

Verify by opening key screenshots:
- `frame_000000.png` — disconnected (dark sky, BLE icon, "No connection")
- `event_*_connect.png` — scene with happy Clawd
- `event_*_notify.png` — notification panel visible
- Middle frames — notification panel with 2 cards
- `event_*_disconnect.png` — back to disconnected state

- [ ] **Step 3: Full end-to-end test — interactive mode**

```bash
./simulator/build/clawd-sim
```

Verify: window appears, keyboard shortcuts work, `s` saves screenshot.

- [ ] **Step 4: Full end-to-end test — scenario file**

```bash
rm -rf shots && ./simulator/build/clawd-sim --headless \
  --scenario simulator/scenarios/demo.json \
  --screenshot-interval 250 --screenshot-on-event \
  --screenshot-dir ./shots/
```

- [ ] **Step 5: Final commit**

```bash
git add simulator/.gitignore
git commit -m "feat(sim): complete Clawd display simulator — headless + interactive + screenshots"
```
