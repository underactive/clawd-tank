# Clawd Notification Display — Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an ESP32-C6 firmware + Python host daemon that shows Claude Code "waiting for input" notifications on a 1.47" LCD with Clawd mascot.

**Architecture:** BLE GATT pipeline — Claude Code hooks fire `clawd-notify` CLI, which forwards events to a Python daemon over Unix socket. The daemon maintains a persistent BLE connection to the ESP32-C6, which renders notifications via LVGL on an ST7789 LCD. UI/UX creative design is deferred to a specialized agent; this plan implements placeholder UI.

**Tech Stack:** ESP-IDF v5.3+ (RISC-V), LVGL v9.x, NimBLE, Python 3.10+, bleak, cJSON

**Spec corrections (discovered during research):**
- Hook config lives in `~/.claude/settings.json`, NOT `~/.claude/hooks.json`
- Dismissal uses `UserPromptSubmit` and `SessionEnd` hooks (no "resolved" notification type exists)
- Hook config uses three-level nesting: `event → [rule] → rule.hooks → [handler]`

---

## File Structure

### Firmware (`firmware/`)

```
firmware/
  CMakeLists.txt                    # Top-level ESP-IDF project
  sdkconfig.defaults                # Default config (BLE, SPI, LVGL)
  main/
    CMakeLists.txt                  # Component registration
    idf_component.yml               # LVGL + ST7789 dependencies
    Kconfig.projbuild               # CONFIG_CLAWD_WIFI_ENABLED
    main.c                          # App entry, FreeRTOS task creation
    display.c                       # ST7789 SPI + LVGL display init
    display.h
    ble_service.c                   # NimBLE GATT server + JSON parsing
    ble_service.h
    notification.c                  # Notification store (pure C, no ESP-IDF deps)
    notification.h
    ui_manager.c                    # LVGL screens, state machine, placeholder UI
    ui_manager.h
  test/
    Makefile                        # Host-side test build (gcc + notification.c)
    test_notification.c             # Unit tests for notification store
```

### Python Host (`host/`)

```
host/
  clawd-notify                      # CLI entry point (shebang script)
  clawd_daemon/
    __init__.py
    daemon.py                       # Async main loop, ties BLE + socket
    ble_client.py                   # bleak BLE connection management
    socket_server.py                # Unix socket listener
    protocol.py                     # Message formats, serialization
  tests/
    test_protocol.py
    test_daemon.py
  requirements.txt                  # bleak
  requirements-dev.txt              # pytest, pytest-asyncio
```

---

## Chunk 1: Firmware Foundation

### Task 1: ESP-IDF Project Scaffold

**Files:**
- Create: `firmware/CMakeLists.txt`
- Create: `firmware/sdkconfig.defaults`
- Create: `firmware/main/CMakeLists.txt`
- Create: `firmware/main/idf_component.yml`
- Create: `firmware/main/Kconfig.projbuild`
- Create: `firmware/main/main.c` (minimal stub)

- [ ] **Step 1: Create top-level CMakeLists.txt**

```cmake
# firmware/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(clawd)
```

- [ ] **Step 2: Create sdkconfig.defaults**

```ini
# firmware/sdkconfig.defaults

# BLE (NimBLE)
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y
CONFIG_BT_NIMBLE_ROLE_CENTRAL=n
CONFIG_BT_NIMBLE_ROLE_OBSERVER=n
CONFIG_BT_NIMBLE_ROLE_BROADCASTER=n
CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=256

# SPI LCD
CONFIG_LCD_HOST_SPI2=y

# LVGL
CONFIG_LV_COLOR_DEPTH_16=y

# FreeRTOS
CONFIG_FREERTOS_HZ=1000
```

- [ ] **Step 3: Create main/CMakeLists.txt**

```cmake
# firmware/main/CMakeLists.txt
idf_component_register(
    SRCS "main.c" "display.c" "ble_service.c" "notification.c" "ui_manager.c"
    INCLUDE_DIRS "."
    REQUIRES esp_lcd driver esp_timer bt nvs_flash freertos json
)
```

- [ ] **Step 4: Create main/idf_component.yml**

```yaml
# firmware/main/idf_component.yml
dependencies:
  lvgl/lvgl: "^9.2"
  espressif/esp_lcd_st7789: "^1.4"
  idf: ">=5.3"
```

- [ ] **Step 5: Create main/Kconfig.projbuild**

```
# firmware/main/Kconfig.projbuild
menu "Clawd Configuration"
    config CLAWD_WIFI_ENABLED
        bool "Enable WiFi (debug/setup)"
        default n
        help
            Enable WiFi for OTA updates and debug logging.
            Not needed for normal BLE notification operation.
endmenu
```

- [ ] **Step 6: Create minimal main.c stub**

```c
// firmware/main/main.c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "clawd";

void app_main(void)
{
    ESP_LOGI(TAG, "Clawd starting...");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

- [ ] **Step 7: Create empty source stubs so the project compiles**

Create empty files so `CMakeLists.txt` doesn't fail:
- `firmware/main/display.c` — empty file with `#include "display.h"`
- `firmware/main/display.h` — empty header guard
- `firmware/main/ble_service.c` — empty file with `#include "ble_service.h"`
- `firmware/main/ble_service.h` — empty header guard
- `firmware/main/notification.c` — empty file with `#include "notification.h"`
- `firmware/main/notification.h` — empty header guard
- `firmware/main/ui_manager.c` — empty file with `#include "ui_manager.h"`
- `firmware/main/ui_manager.h` — empty header guard

- [ ] **Step 8: Verify the project builds**

```bash
cd firmware
idf.py set-target esp32c6
idf.py build
```

Expected: Build succeeds. The first build downloads LVGL and ST7789 components via the component manager.

- [ ] **Step 9: Commit**

```bash
git add firmware/
git commit -m "feat: scaffold ESP-IDF project for Clawd"
```

---

### Task 2: Notification Data Store + Host Tests

**Files:**
- Create: `firmware/main/notification.h`
- Create: `firmware/main/notification.c`
- Create: `firmware/test/Makefile`
- Create: `firmware/test/test_notification.c`

This module is pure C with zero ESP-IDF dependencies so it can be tested on the host machine.

- [ ] **Step 1: Write notification.h**

```c
// firmware/main/notification.h
#ifndef NOTIFICATION_H
#define NOTIFICATION_H

#include <stdbool.h>

#define NOTIF_MAX_COUNT    8
#define NOTIF_MAX_ID_LEN   48
#define NOTIF_MAX_PROJ_LEN 32
#define NOTIF_MAX_MSG_LEN  64

typedef struct {
    char id[NOTIF_MAX_ID_LEN];
    char project[NOTIF_MAX_PROJ_LEN];
    char message[NOTIF_MAX_MSG_LEN];
    bool active;
} notification_t;

typedef struct {
    notification_t items[NOTIF_MAX_COUNT];
    int count;
} notification_store_t;

void notif_store_init(notification_store_t *store);

// Returns 0 on success, -1 on error. If ID exists, updates it.
// If full, drops oldest to make room.
int notif_store_add(notification_store_t *store,
                    const char *id, const char *project, const char *message);

// Returns 0 if found and removed, -1 if not found (idempotent).
int notif_store_dismiss(notification_store_t *store, const char *id);

void notif_store_clear(notification_store_t *store);

int notif_store_count(const notification_store_t *store);

// Returns NULL if index out of range or slot inactive.
const notification_t *notif_store_get(const notification_store_t *store, int index);

#endif // NOTIFICATION_H
```

- [ ] **Step 2: Write the host-side test**

```c
// firmware/test/test_notification.c
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../main/notification.h"

static notification_store_t store;

static void test_init_empty(void) {
    notif_store_init(&store);
    assert(notif_store_count(&store) == 0);
    assert(notif_store_get(&store, 0) == NULL);
    printf("  PASS: test_init_empty\n");
}

static void test_add_one(void) {
    notif_store_init(&store);
    int rc = notif_store_add(&store, "s1", "my-project", "Waiting for input");
    assert(rc == 0);
    assert(notif_store_count(&store) == 1);
    const notification_t *n = notif_store_get(&store, 0);
    assert(n != NULL);
    assert(strcmp(n->id, "s1") == 0);
    assert(strcmp(n->project, "my-project") == 0);
    assert(strcmp(n->message, "Waiting for input") == 0);
    printf("  PASS: test_add_one\n");
}

static void test_dismiss(void) {
    notif_store_init(&store);
    notif_store_add(&store, "s1", "proj", "msg");
    notif_store_add(&store, "s2", "proj", "msg");
    assert(notif_store_count(&store) == 2);

    int rc = notif_store_dismiss(&store, "s1");
    assert(rc == 0);
    assert(notif_store_count(&store) == 1);

    // Dismiss nonexistent is idempotent
    rc = notif_store_dismiss(&store, "s1");
    assert(rc == -1);
    assert(notif_store_count(&store) == 1);
    printf("  PASS: test_dismiss\n");
}

static void test_clear(void) {
    notif_store_init(&store);
    notif_store_add(&store, "s1", "p", "m");
    notif_store_add(&store, "s2", "p", "m");
    notif_store_clear(&store);
    assert(notif_store_count(&store) == 0);
    printf("  PASS: test_clear\n");
}

static void test_update_existing(void) {
    notif_store_init(&store);
    notif_store_add(&store, "s1", "proj", "first");
    notif_store_add(&store, "s1", "proj", "updated");
    assert(notif_store_count(&store) == 1);
    const notification_t *n = notif_store_get(&store, 0);
    assert(strcmp(n->message, "updated") == 0);
    printf("  PASS: test_update_existing\n");
}

static void test_overflow_drops_oldest(void) {
    notif_store_init(&store);
    char id[8];
    for (int i = 0; i < NOTIF_MAX_COUNT; i++) {
        snprintf(id, sizeof(id), "s%d", i);
        notif_store_add(&store, id, "p", "m");
    }
    assert(notif_store_count(&store) == NOTIF_MAX_COUNT);

    // Add 9th — should drop "s0"
    notif_store_add(&store, "s_new", "p", "m");
    assert(notif_store_count(&store) == NOTIF_MAX_COUNT);

    // "s0" should be gone
    int found_s0 = 0;
    int found_new = 0;
    for (int i = 0; i < NOTIF_MAX_COUNT; i++) {
        const notification_t *n = notif_store_get(&store, i);
        if (n && strcmp(n->id, "s0") == 0) found_s0 = 1;
        if (n && strcmp(n->id, "s_new") == 0) found_new = 1;
    }
    assert(!found_s0);
    assert(found_new);
    printf("  PASS: test_overflow_drops_oldest\n");
}

int main(void) {
    printf("Running notification store tests...\n");
    test_init_empty();
    test_add_one();
    test_dismiss();
    test_clear();
    test_update_existing();
    test_overflow_drops_oldest();
    printf("All tests passed!\n");
    return 0;
}
```

- [ ] **Step 3: Create test Makefile**

```makefile
# firmware/test/Makefile
CC = gcc
CFLAGS = -Wall -Wextra -I../main

test: test_notification
	./test_notification

test_notification: test_notification.c ../main/notification.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f test_notification

.PHONY: test clean
```

- [ ] **Step 4: Run tests — verify they fail**

```bash
cd firmware/test
make test
```

Expected: Linker errors — `notif_store_*` functions not defined (notification.c is still empty).

- [ ] **Step 5: Implement notification.c**

```c
// firmware/main/notification.c
#include "notification.h"
#include <string.h>

void notif_store_init(notification_store_t *store) {
    memset(store, 0, sizeof(*store));
}

// Find slot by ID. Returns index or -1.
static int find_by_id(const notification_store_t *store, const char *id) {
    for (int i = 0; i < NOTIF_MAX_COUNT; i++) {
        if (store->items[i].active && strcmp(store->items[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

// Find first inactive slot. Returns index or -1.
static int find_free_slot(const notification_store_t *store) {
    for (int i = 0; i < NOTIF_MAX_COUNT; i++) {
        if (!store->items[i].active) {
            return i;
        }
    }
    return -1;
}

// Find oldest active slot (lowest index). Returns index or -1.
static int find_oldest(const notification_store_t *store) {
    for (int i = 0; i < NOTIF_MAX_COUNT; i++) {
        if (store->items[i].active) {
            return i;
        }
    }
    return -1;
}

static void write_slot(notification_t *slot, const char *id,
                        const char *project, const char *message) {
    strncpy(slot->id, id, NOTIF_MAX_ID_LEN - 1);
    slot->id[NOTIF_MAX_ID_LEN - 1] = '\0';
    strncpy(slot->project, project, NOTIF_MAX_PROJ_LEN - 1);
    slot->project[NOTIF_MAX_PROJ_LEN - 1] = '\0';
    strncpy(slot->message, message, NOTIF_MAX_MSG_LEN - 1);
    slot->message[NOTIF_MAX_MSG_LEN - 1] = '\0';
    slot->active = true;
}

int notif_store_add(notification_store_t *store,
                    const char *id, const char *project, const char *message) {
    // Update existing?
    int idx = find_by_id(store, id);
    if (idx >= 0) {
        write_slot(&store->items[idx], id, project, message);
        return 0;
    }

    // Find free slot
    idx = find_free_slot(store);
    if (idx >= 0) {
        write_slot(&store->items[idx], id, project, message);
        store->count++;
        return 0;
    }

    // Full — drop oldest
    idx = find_oldest(store);
    if (idx >= 0) {
        write_slot(&store->items[idx], id, project, message);
        // count stays the same (replaced one)
        return 0;
    }

    return -1; // Should never happen
}

int notif_store_dismiss(notification_store_t *store, const char *id) {
    int idx = find_by_id(store, id);
    if (idx < 0) return -1;

    store->items[idx].active = false;
    memset(&store->items[idx], 0, sizeof(notification_t));
    store->count--;
    return 0;
}

void notif_store_clear(notification_store_t *store) {
    notif_store_init(store);
}

int notif_store_count(const notification_store_t *store) {
    return store->count;
}

const notification_t *notif_store_get(const notification_store_t *store, int index) {
    if (index < 0 || index >= NOTIF_MAX_COUNT) return NULL;
    if (!store->items[index].active) return NULL;
    return &store->items[index];
}
```

- [ ] **Step 6: Run tests — verify they pass**

```bash
cd firmware/test
make test
```

Expected: `All tests passed!`

- [ ] **Step 7: Commit**

```bash
git add firmware/main/notification.c firmware/main/notification.h firmware/test/
git commit -m "feat: add notification store with host-side tests"
```

---

### Task 3: ST7789 Display Driver + LVGL Initialization

**Files:**
- Create: `firmware/main/display.h`
- Create: `firmware/main/display.c`

**Reference:** Waveshare ESP32-C6-LCD-1.47 pinout:
- MOSI=GPIO6, SCLK=GPIO7, CS=GPIO14, DC=GPIO15, RST=GPIO21, BL=GPIO22
- Resolution: 172x320 (portrait native), used as 320x172 (landscape)
- Color order: BGR with inversion ON
- Landscape offset: offset_x=34, offset_y=0

- [ ] **Step 1: Write display.h**

```c
// firmware/main/display.h
#ifndef DISPLAY_H
#define DISPLAY_H

#include "lvgl.h"

// Initialize SPI bus, ST7789 panel, LVGL display, and tick timer.
// Returns the LVGL display object. Starts backlight.
lv_display_t *display_init(void);

#endif // DISPLAY_H
```

- [ ] **Step 2: Write display.c**

```c
// firmware/main/display.c
#include "display.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7789.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "display";

// Waveshare ESP32-C6-LCD-1.47 pin definitions
#define PIN_MOSI    6
#define PIN_SCLK    7
#define PIN_CS      14
#define PIN_DC      15
#define PIN_RST     21
#define PIN_BL      22

// Display config
#define LCD_HOST        SPI2_HOST
#define LCD_PIXEL_CLK   (20 * 1000 * 1000)
#define LCD_H_RES       320   // landscape width
#define LCD_V_RES       172   // landscape height
#define LCD_CMD_BITS    8
#define LCD_PARAM_BITS  8
#define LVGL_BUF_LINES  20
#define LVGL_TICK_MS    2

static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                                     esp_lcd_panel_io_event_data_t *edata,
                                     void *user_ctx) {
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area,
                           uint8_t *px_map) {
    esp_lcd_panel_handle_t panel = lv_display_get_user_data(disp);

    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;
    lv_draw_sw_rgb565_swap(px_map, w * h);

    esp_lcd_panel_draw_bitmap(panel,
        area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
}

static void lvgl_tick_cb(void *arg) {
    lv_tick_inc(LVGL_TICK_MS);
}

lv_display_t *display_init(void) {
    ESP_LOGI(TAG, "Initializing display...");

    // Backlight off during init
    gpio_config_t bl_cfg = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << PIN_BL,
    };
    gpio_config(&bl_cfg);
    gpio_set_level(PIN_BL, 0);

    // SPI bus
    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_SCLK,
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LVGL_BUF_LINES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // Panel I/O
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_DC,
        .cs_gpio_num = PIN_CS,
        .pclk_hz = LCD_PIXEL_CLK,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_HOST, &io_config, &io_handle));

    // ST7789 panel
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));

    // Landscape: swap X/Y, then mirror as needed
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, false, true));
    // Apply offset for 172-pixel dimension (centered in 240-pixel controller RAM)
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel, 34, 0));

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
    gpio_set_level(PIN_BL, 1);

    // LVGL init
    lv_init();

    lv_display_t *display = lv_display_create(LCD_H_RES, LCD_V_RES);

    // DMA buffers
    size_t buf_sz = LCD_H_RES * LVGL_BUF_LINES * sizeof(lv_color16_t);
    void *buf1 = heap_caps_malloc(buf_sz, MALLOC_CAP_DMA);
    void *buf2 = heap_caps_malloc(buf_sz, MALLOC_CAP_DMA);
    assert(buf1 && buf2);

    lv_display_set_buffers(display, buf1, buf2, buf_sz,
                            LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_user_data(display, panel);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, lvgl_flush_cb);

    // DMA done → flush ready callback
    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = notify_lvgl_flush_ready,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io_handle, &cbs, display));

    // LVGL tick timer
    const esp_timer_create_args_t tick_args = {
        .callback = &lvgl_tick_cb,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, LVGL_TICK_MS * 1000));

    ESP_LOGI(TAG, "Display initialized: %dx%d landscape", LCD_H_RES, LCD_V_RES);
    return display;
}
```

- [ ] **Step 3: Update main.c to init display and run LVGL with a test label**

```c
// firmware/main/main.c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "display.h"
#include "lvgl.h"

static const char *TAG = "clawd";

static _lock_t lvgl_lock;

static void ui_task(void *arg) {
    lv_display_t *display = (lv_display_t *)arg;
    (void)display;

    _lock_acquire(&lvgl_lock);
    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "Clawd ready!");
    lv_obj_center(label);
    _lock_release(&lvgl_lock);

    while (1) {
        _lock_acquire(&lvgl_lock);
        lv_timer_handler();
        _lock_release(&lvgl_lock);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Clawd starting...");

    lv_display_t *display = display_init();

    xTaskCreate(ui_task, "ui_task", 4096, display, 5, NULL);
}
```

- [ ] **Step 4: Build and flash to verify display works**

```bash
cd firmware
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Expected: LCD shows "Clawd ready!" text centered on screen. Adjust `flash monitor` port as needed (`/dev/ttyACM0` or `/dev/cu.usbmodem*` on macOS).

- [ ] **Step 5: Commit**

```bash
git add firmware/main/display.c firmware/main/display.h firmware/main/main.c
git commit -m "feat: initialize ST7789 display with LVGL in landscape mode"
```

---

## Chunk 2: Firmware BLE & UI

### Task 4: NimBLE GATT Service

**Files:**
- Create: `firmware/main/ble_service.h`
- Create: `firmware/main/ble_service.c`

**UUIDs (from spec):**
- Service: `AECBEFD9-98A2-4773-9FED-BB2166DAA49A`
- Notification write characteristic: `71FFB137-8B7A-47C9-9A7A-4B1B16662D9A`

- [ ] **Step 1: Write ble_service.h**

```c
// firmware/main/ble_service.h
#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Event types posted to the UI queue
typedef enum {
    BLE_EVT_NOTIF_ADD,
    BLE_EVT_NOTIF_DISMISS,
    BLE_EVT_NOTIF_CLEAR,
    BLE_EVT_CONNECTED,
    BLE_EVT_DISCONNECTED,
} ble_evt_type_t;

typedef struct {
    ble_evt_type_t type;
    char id[48];
    char project[32];
    char message[64];
} ble_evt_t;

// Initialize NimBLE stack and GATT server.
// Events are posted to the provided queue.
void ble_service_init(QueueHandle_t evt_queue);

#endif // BLE_SERVICE_H
```

- [ ] **Step 2: Write ble_service.c**

```c
// firmware/main/ble_service.c
#include "ble_service.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "ble";
static QueueHandle_t s_evt_queue;

// UUIDs in little-endian byte order
// Service: AECBEFD9-98A2-4773-9FED-BB2166DAA49A
static const ble_uuid128_t clawd_svc_uuid = BLE_UUID128_INIT(
    0x9a, 0xa4, 0xda, 0x66, 0x21, 0xbb, 0xed, 0x9f,
    0x73, 0x47, 0xa2, 0x98, 0xd9, 0xef, 0xcb, 0xae
);

// Characteristic: 71FFB137-8B7A-47C9-9A7A-4B1B16662D9A
static const ble_uuid128_t notif_chr_uuid = BLE_UUID128_INIT(
    0x9a, 0x2d, 0x66, 0x16, 0x1b, 0x4b, 0x7a, 0x9a,
    0xc9, 0x47, 0x7a, 0x8b, 0x37, 0xb1, 0xff, 0x71
);

// Forward declarations
static int ble_gap_event_cb(struct ble_gap_event *event, void *arg);
static void start_advertising(void);

static void safe_strncpy(char *dst, const char *src, size_t n) {
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, n - 1);
    dst[n - 1] = '\0';
}

static void parse_notification_json(const char *buf, uint16_t len) {
    cJSON *json = cJSON_ParseWithLength(buf, len);
    if (!json) {
        ESP_LOGW(TAG, "Malformed JSON, ignoring");
        return;
    }

    cJSON *action = cJSON_GetObjectItem(json, "action");
    if (!action || !cJSON_IsString(action)) {
        ESP_LOGW(TAG, "Missing 'action' field, ignoring");
        cJSON_Delete(json);
        return;
    }

    ble_evt_t evt = {0};

    if (strcmp(action->valuestring, "add") == 0) {
        evt.type = BLE_EVT_NOTIF_ADD;
        cJSON *id = cJSON_GetObjectItem(json, "id");
        cJSON *project = cJSON_GetObjectItem(json, "project");
        cJSON *message = cJSON_GetObjectItem(json, "message");
        if (!id || !cJSON_IsString(id)) {
            cJSON_Delete(json);
            return;
        }
        safe_strncpy(evt.id, id->valuestring, sizeof(evt.id));
        safe_strncpy(evt.project,
                     project && cJSON_IsString(project) ? project->valuestring : "",
                     sizeof(evt.project));
        safe_strncpy(evt.message,
                     message && cJSON_IsString(message) ? message->valuestring : "",
                     sizeof(evt.message));
    } else if (strcmp(action->valuestring, "dismiss") == 0) {
        evt.type = BLE_EVT_NOTIF_DISMISS;
        cJSON *id = cJSON_GetObjectItem(json, "id");
        if (!id || !cJSON_IsString(id)) {
            cJSON_Delete(json);
            return;
        }
        safe_strncpy(evt.id, id->valuestring, sizeof(evt.id));
    } else if (strcmp(action->valuestring, "clear") == 0) {
        evt.type = BLE_EVT_NOTIF_CLEAR;
    } else {
        ESP_LOGW(TAG, "Unknown action '%s', ignoring", action->valuestring);
        cJSON_Delete(json);
        return;
    }

    cJSON_Delete(json);
    xQueueSend(s_evt_queue, &evt, pdMS_TO_TICKS(100));
}

static int notification_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return 0;

    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len == 0 || len > 512) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    char buf[513];
    uint16_t copied;
    ble_hs_mbuf_to_flat(ctxt->om, buf, len, &copied);
    buf[copied] = '\0';

    ESP_LOGD(TAG, "BLE write (%d bytes): %s", copied, buf);
    parse_notification_json(buf, copied);
    return 0;
}

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &clawd_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &notif_chr_uuid.u,
                .access_cb = notification_write_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            { 0 },
        },
    },
    { 0 },
};

static void start_advertising(void) {
    struct ble_gap_adv_params adv_params = {0};
    struct ble_hs_adv_fields fields = {0};

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)"Clawd";
    fields.name_len = 5;
    fields.name_is_complete = 1;
    ble_gap_adv_set_fields(&fields);

    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                      &adv_params, ble_gap_event_cb, NULL);
}

static int ble_gap_event_cb(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "BLE %s", event->connect.status == 0 ? "connected" : "connect failed");
        if (event->connect.status == 0) {
            ble_evt_t evt = { .type = BLE_EVT_CONNECTED };
            xQueueSend(s_evt_queue, &evt, pdMS_TO_TICKS(100));
        } else {
            start_advertising();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "BLE disconnected");
        ble_evt_t evt = { .type = BLE_EVT_DISCONNECTED };
        xQueueSend(s_evt_queue, &evt, pdMS_TO_TICKS(100));
        start_advertising();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        break;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU updated to %d", event->mtu.value);
        break;
    }
    return 0;
}

static void ble_on_sync(void) {
    ESP_LOGI(TAG, "BLE synced, starting advertising as 'Clawd'");
    start_advertising();
}

static void ble_host_task(void *param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_service_init(QueueHandle_t evt_queue) {
    s_evt_queue = evt_queue;

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(nimble_port_init());

    ble_svc_gap_device_name_set("Clawd");
    ble_svc_gap_init();
    ble_svc_gatt_init();

    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);

    ble_hs_cfg.sync_cb = ble_on_sync;

    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE service initialized");
}
```

- [ ] **Step 3: Build to verify compilation**

```bash
cd firmware
idf.py build
```

Expected: Build succeeds. BLE service is not wired into `main.c` yet — just verifying it compiles.

- [ ] **Step 4: Commit**

```bash
git add firmware/main/ble_service.c firmware/main/ble_service.h
git commit -m "feat: add NimBLE GATT server with notification JSON parsing"
```

---

### Task 5: UI Manager (Placeholder)

**Files:**
- Create: `firmware/main/ui_manager.h`
- Create: `firmware/main/ui_manager.c`

This implements the notification state machine with placeholder text-only UI. The visual design (Clawd sprites, animations, colors) will be replaced by a UI/UX agent later.

- [ ] **Step 1: Write ui_manager.h**

```c
// firmware/main/ui_manager.h
#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include "lvgl.h"
#include "ble_service.h"

// Initialize UI manager. Must be called after lv_init() and display_init().
void ui_manager_init(void);

// Process a BLE event. Called from the UI task loop.
void ui_manager_handle_event(const ble_evt_t *evt);

// Run one iteration of LVGL timer handler. Called from the UI task loop.
void ui_manager_tick(void);

#endif // UI_MANAGER_H
```

- [ ] **Step 2: Write ui_manager.c**

```c
// firmware/main/ui_manager.c
#include "ui_manager.h"
#include "notification.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "ui";

typedef enum {
    UI_STATE_IDLE,
    UI_STATE_NOTIFICATION,
    UI_STATE_LIST,
    UI_STATE_DISCONNECTED,
} ui_state_t;

static ui_state_t s_state = UI_STATE_DISCONNECTED;
static notification_store_t s_store;
static _lock_t s_lock;

// LVGL objects (placeholder UI)
static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_list_container = NULL;

static void rebuild_ui(void);

void ui_manager_init(void) {
    notif_store_init(&s_store);
    s_screen = lv_screen_active();

    // Status label (top area — placeholder for Clawd)
    s_status_label = lv_label_create(s_screen);
    lv_obj_align(s_status_label, LV_ALIGN_TOP_LEFT, 4, 4);
    lv_label_set_text(s_status_label, "[Clawd] Disconnected");

    // Notification list area
    s_list_container = lv_obj_create(s_screen);
    lv_obj_set_size(s_list_container, 200, 150);
    lv_obj_align(s_list_container, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_set_flex_flow(s_list_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(s_list_container, 2, 0);

    rebuild_ui();
    ESP_LOGI(TAG, "UI manager initialized");
}

static void rebuild_ui(void) {
    // Update status label
    int count = notif_store_count(&s_store);

    switch (s_state) {
    case UI_STATE_DISCONNECTED:
        lv_label_set_text(s_status_label, "[Clawd] zzz\nDisconnected");
        break;
    case UI_STATE_IDLE:
        lv_label_set_text(s_status_label, "[Clawd] :)\nAll clear!");
        break;
    case UI_STATE_NOTIFICATION:
    case UI_STATE_LIST:
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "[Clawd] !!\n%d waiting", count);
            lv_label_set_text(s_status_label, buf);
        }
        break;
    }

    // Rebuild notification list
    lv_obj_clean(s_list_container);

    if (count == 0) {
        lv_obj_t *empty = lv_label_create(s_list_container);
        lv_label_set_text(empty, "No notifications");
        return;
    }

    for (int i = 0; i < NOTIF_MAX_COUNT; i++) {
        const notification_t *n = notif_store_get(&s_store, i);
        if (!n) continue;

        lv_obj_t *row = lv_label_create(s_list_container);
        char buf[96];
        snprintf(buf, sizeof(buf), "> %s\n  %s", n->project, n->message);
        lv_label_set_text(row, buf);
        lv_label_set_long_mode(row, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(row, 190);
    }
}

static void update_state(void) {
    int count = notif_store_count(&s_store);
    if (s_state == UI_STATE_DISCONNECTED) return;

    if (count == 0) {
        s_state = UI_STATE_IDLE;
    } else {
        s_state = UI_STATE_LIST;
    }
}

void ui_manager_handle_event(const ble_evt_t *evt) {
    _lock_acquire(&s_lock);

    switch (evt->type) {
    case BLE_EVT_CONNECTED:
        ESP_LOGI(TAG, "Connected");
        s_state = UI_STATE_IDLE;
        break;

    case BLE_EVT_DISCONNECTED:
        ESP_LOGI(TAG, "Disconnected");
        s_state = UI_STATE_DISCONNECTED;
        notif_store_clear(&s_store);
        break;

    case BLE_EVT_NOTIF_ADD:
        ESP_LOGI(TAG, "Add: %s (%s)", evt->id, evt->project);
        notif_store_add(&s_store, evt->id, evt->project, evt->message);
        s_state = UI_STATE_NOTIFICATION;
        break;

    case BLE_EVT_NOTIF_DISMISS:
        ESP_LOGI(TAG, "Dismiss: %s", evt->id);
        notif_store_dismiss(&s_store, evt->id);
        update_state();
        break;

    case BLE_EVT_NOTIF_CLEAR:
        ESP_LOGI(TAG, "Clear all");
        notif_store_clear(&s_store);
        update_state();
        break;
    }

    rebuild_ui();
    _lock_release(&s_lock);
}

void ui_manager_tick(void) {
    _lock_acquire(&s_lock);
    lv_timer_handler();
    _lock_release(&s_lock);
}
```

- [ ] **Step 3: Build to verify compilation**

```bash
cd firmware
idf.py build
```

Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add firmware/main/ui_manager.c firmware/main/ui_manager.h
git commit -m "feat: add placeholder UI manager with notification state machine"
```

---

### Task 6: Main App Integration

**Files:**
- Modify: `firmware/main/main.c`

Wire everything together: display init, BLE init, UI task loop with event queue.

- [ ] **Step 1: Rewrite main.c to integrate all components**

```c
// firmware/main/main.c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "display.h"
#include "ble_service.h"
#include "ui_manager.h"

static const char *TAG = "clawd";

#define EVT_QUEUE_LEN 16

static QueueHandle_t s_evt_queue;

static void ui_task(void *arg) {
    ui_manager_init();

    ble_evt_t evt;
    while (1) {
        // Process any pending BLE events
        while (xQueueReceive(s_evt_queue, &evt, 0) == pdTRUE) {
            ui_manager_handle_event(&evt);
        }

        // Run LVGL
        ui_manager_tick();

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Clawd starting...");

    // Create event queue (BLE → UI)
    s_evt_queue = xQueueCreate(EVT_QUEUE_LEN, sizeof(ble_evt_t));
    assert(s_evt_queue);

    // Init display (SPI + LVGL)
    display_init();

    // Init BLE (NimBLE GATT server, posts events to queue)
    ble_service_init(s_evt_queue);

    // Start UI task
    xTaskCreate(ui_task, "ui_task", 8192, NULL, 5, NULL);

    ESP_LOGI(TAG, "Clawd running");
}
```

- [ ] **Step 2: Build and flash**

```bash
cd firmware
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Expected: Device boots, LCD shows `[Clawd] zzz / Disconnected`. Serial log shows `Clawd starting...`, `BLE synced, starting advertising as 'Clawd'`.

- [ ] **Step 3: Verify BLE advertising**

On your Mac, open a BLE scanner app (e.g., LightBlue, nRF Connect) and verify you see a device named "Clawd" advertising.

- [ ] **Step 4: Commit**

```bash
git add firmware/main/main.c
git commit -m "feat: integrate display, BLE, and UI into main app"
```

---

## Chunk 3: Python Host & Integration

### Task 7: Python Project Setup + Protocol Module

**Files:**
- Create: `host/requirements.txt`
- Create: `host/requirements-dev.txt`
- Create: `host/clawd_daemon/__init__.py`
- Create: `host/clawd_daemon/protocol.py`
- Create: `host/tests/test_protocol.py`

- [ ] **Step 1: Create .gitignore and requirements files**

```
# host/.gitignore
.venv/
__pycache__/
*.pyc
```



```
# host/requirements.txt
bleak>=0.21.0
```

```
# host/requirements-dev.txt
-r requirements.txt
pytest>=7.0
pytest-asyncio>=0.21
```

- [ ] **Step 2: Create virtual environment**

```bash
cd host
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements-dev.txt
```

- [ ] **Step 3: Write the test for protocol module**

```python
# host/tests/test_protocol.py
import json
from clawd_daemon.protocol import (
    hook_payload_to_daemon_message,
    daemon_message_to_ble_payload,
)


def test_idle_prompt_to_add():
    hook = {
        "hook_event_name": "Notification",
        "notification_type": "idle_prompt",
        "session_id": "abc-123",
        "cwd": "/Users/me/Projects/my-project",
        "message": "Claude is waiting for input",
    }
    msg = hook_payload_to_daemon_message(hook)
    assert msg is not None
    assert msg["event"] == "add"
    assert msg["session_id"] == "abc-123"
    assert msg["project"] == "my-project"
    assert msg["message"] == "Claude is waiting for input"


def test_prompt_submit_to_dismiss():
    hook = {
        "hook_event_name": "UserPromptSubmit",
        "session_id": "abc-123",
        "cwd": "/Users/me/Projects/my-project",
    }
    msg = hook_payload_to_daemon_message(hook)
    assert msg is not None
    assert msg["event"] == "dismiss"
    assert msg["session_id"] == "abc-123"


def test_session_end_to_dismiss():
    hook = {
        "hook_event_name": "SessionEnd",
        "session_id": "abc-123",
        "cwd": "/Users/me/Projects/foo",
    }
    msg = hook_payload_to_daemon_message(hook)
    assert msg is not None
    assert msg["event"] == "dismiss"
    assert msg["session_id"] == "abc-123"


def test_irrelevant_notification_ignored():
    hook = {
        "hook_event_name": "Notification",
        "notification_type": "auth_success",
        "session_id": "abc-123",
        "cwd": "/tmp",
    }
    msg = hook_payload_to_daemon_message(hook)
    assert msg is None


def test_daemon_add_to_ble():
    msg = {"event": "add", "session_id": "s1", "project": "proj", "message": "hi"}
    ble = daemon_message_to_ble_payload(msg)
    parsed = json.loads(ble)
    assert parsed["action"] == "add"
    assert parsed["id"] == "s1"
    assert parsed["project"] == "proj"
    assert parsed["message"] == "hi"


def test_daemon_dismiss_to_ble():
    msg = {"event": "dismiss", "session_id": "s1"}
    ble = daemon_message_to_ble_payload(msg)
    parsed = json.loads(ble)
    assert parsed["action"] == "dismiss"
    assert parsed["id"] == "s1"
```

- [ ] **Step 4: Run tests — verify they fail**

```bash
cd host
source .venv/bin/activate
python -m pytest tests/test_protocol.py -v
```

Expected: ImportError — module doesn't exist yet.

- [ ] **Step 5: Implement protocol.py**

```python
# host/clawd_daemon/__init__.py
# (empty)
```

```python
# host/clawd_daemon/protocol.py
"""Message format conversion between Claude Code hooks, daemon, and BLE."""

import json
import os
from typing import Optional


def hook_payload_to_daemon_message(hook: dict) -> Optional[dict]:
    """Convert a Claude Code hook stdin payload to a daemon message.

    Returns None if the hook event is not relevant (should be ignored).
    """
    event_name = hook.get("hook_event_name", "")
    session_id = hook.get("session_id", "")

    if event_name == "Notification":
        if hook.get("notification_type") != "idle_prompt":
            return None
        cwd = hook.get("cwd", "")
        project = os.path.basename(cwd) if cwd else "unknown"
        message = hook.get("message", "Waiting for input")
        return {
            "event": "add",
            "session_id": session_id,
            "project": project,
            "message": message,
        }

    if event_name in ("UserPromptSubmit", "SessionEnd"):
        return {
            "event": "dismiss",
            "session_id": session_id,
        }

    return None


def daemon_message_to_ble_payload(msg: dict) -> str:
    """Convert a daemon message to a JSON string for BLE GATT write."""
    event = msg["event"]

    if event == "add":
        return json.dumps({
            "action": "add",
            "id": msg["session_id"],
            "project": msg.get("project", ""),
            "message": msg.get("message", ""),
        })

    if event == "dismiss":
        return json.dumps({
            "action": "dismiss",
            "id": msg["session_id"],
        })

    if event == "clear":
        return json.dumps({"action": "clear"})

    raise ValueError(f"Unknown event: {event}")
```

- [ ] **Step 6: Run tests — verify they pass**

```bash
cd host
source .venv/bin/activate
python -m pytest tests/test_protocol.py -v
```

Expected: All 6 tests pass.

- [ ] **Step 7: Commit**

```bash
git add host/.gitignore host/requirements.txt host/requirements-dev.txt host/clawd_daemon/__init__.py host/clawd_daemon/protocol.py host/tests/test_protocol.py
git commit -m "feat: add Python project scaffold and protocol module with tests"
```

---

### Task 8: BLE Client

**Files:**
- Create: `host/clawd_daemon/ble_client.py`

- [ ] **Step 1: Write ble_client.py**

```python
# host/clawd_daemon/ble_client.py
"""BLE GATT client for communicating with the Clawd ESP32 device."""

import asyncio
import logging
from bleak import BleakClient, BleakScanner

logger = logging.getLogger("clawd.ble")

SERVICE_UUID = "aecbefd9-98a2-4773-9fed-bb2166daa49a"
NOTIFICATION_CHR_UUID = "71ffb137-8b7a-47c9-9a7a-4b1b16662d9a"
SCAN_INTERVAL_SECS = 5


class ClawdBleClient:
    """Manages BLE connection to the Clawd ESP32 device."""

    def __init__(self):
        self._client: BleakClient | None = None
        self._connected = asyncio.Event()
        self._lock = asyncio.Lock()

    @property
    def is_connected(self) -> bool:
        return self._client is not None and self._client.is_connected

    async def connect(self) -> None:
        """Scan for and connect to the Clawd device. Retries until found."""
        while True:
            logger.info("Scanning for Clawd device...")
            device = await BleakScanner.find_device_by_name(
                "Clawd", timeout=SCAN_INTERVAL_SECS
            )
            if device is None:
                logger.debug("Clawd not found, retrying...")
                continue

            logger.info("Found Clawd: %s (%s)", device.name, device.address)
            try:
                client = BleakClient(
                    device,
                    disconnected_callback=self._on_disconnect,
                )
                await client.connect()
                self._client = client
                self._connected.set()
                logger.info("Connected to Clawd (MTU: %d)", client.mtu_size)
                return
            except Exception as e:
                logger.warning("Connection failed: %s, retrying...", e)
                await asyncio.sleep(SCAN_INTERVAL_SECS)

    def _on_disconnect(self, client: BleakClient) -> None:
        logger.warning("Disconnected from Clawd")
        self._connected.clear()
        self._client = None

    async def ensure_connected(self) -> None:
        """Reconnect if disconnected."""
        if not self.is_connected:
            await self.connect()

    async def write_notification(self, payload: str) -> bool:
        """Write a JSON payload to the notification characteristic.

        Returns True on success, False on failure.
        """
        async with self._lock:
            if not self.is_connected:
                logger.warning("Not connected, cannot write")
                return False
            try:
                data = payload.encode("utf-8")
                await self._client.write_gatt_char(
                    NOTIFICATION_CHR_UUID, data, response=False
                )
                logger.debug("Wrote %d bytes to BLE", len(data))
                return True
            except Exception as e:
                logger.error("BLE write failed: %s", e)
                return False

    async def disconnect(self) -> None:
        """Disconnect from the device."""
        if self._client and self._client.is_connected:
            await self._client.disconnect()
        self._client = None
        self._connected.clear()
```

- [ ] **Step 2: Commit**

```bash
git add host/clawd_daemon/ble_client.py
git commit -m "feat: add BLE client with auto-reconnect"
```

---

### Task 9: Socket Server + Daemon

**Files:**
- Create: `host/clawd_daemon/socket_server.py`
- Create: `host/clawd_daemon/daemon.py`
- Create: `host/tests/test_daemon.py`

- [ ] **Step 1: Write socket_server.py**

```python
# host/clawd_daemon/socket_server.py
"""Unix socket server that receives hook messages from clawd-notify."""

import asyncio
import json
import logging
import os
from pathlib import Path
from typing import Callable, Awaitable

logger = logging.getLogger("clawd.socket")

SOCKET_PATH = Path.home() / ".clawd" / "sock"


class SocketServer:
    """Listens on a Unix socket for JSON messages from clawd-notify."""

    def __init__(self, on_message: Callable[[dict], Awaitable[None]],
                 socket_path: Path = SOCKET_PATH):
        self._on_message = on_message
        self._socket_path = socket_path
        self._server: asyncio.Server | None = None

    async def start(self) -> None:
        self._socket_path.parent.mkdir(parents=True, exist_ok=True)
        if self._socket_path.exists():
            self._socket_path.unlink()

        self._server = await asyncio.start_unix_server(
            self._handle_client, path=str(self._socket_path)
        )
        # Make socket writable by owner
        os.chmod(self._socket_path, 0o600)
        logger.info("Listening on %s", self._socket_path)

    async def _handle_client(self, reader: asyncio.StreamReader,
                              writer: asyncio.StreamWriter) -> None:
        try:
            data = await asyncio.wait_for(reader.read(4096), timeout=5.0)
            if data:
                msg = json.loads(data.decode("utf-8"))
                await self._on_message(msg)
        except Exception as e:
            logger.error("Error handling socket message: %s", e)
        finally:
            writer.close()
            await writer.wait_closed()

    async def stop(self) -> None:
        if self._server:
            self._server.close()
            await self._server.wait_closed()
        if self._socket_path.exists():
            self._socket_path.unlink()
```

- [ ] **Step 2: Write daemon.py**

```python
# host/clawd_daemon/daemon.py
"""Clawd daemon — bridges Claude Code hooks to ESP32 via BLE."""

import asyncio
import logging
import os
import signal
import sys
from pathlib import Path

from .ble_client import ClawdBleClient
from .protocol import daemon_message_to_ble_payload
from .socket_server import SocketServer

logger = logging.getLogger("clawd")

PID_PATH = Path.home() / ".clawd" / "daemon.pid"


class ClawdDaemon:
    def __init__(self):
        self._ble = ClawdBleClient()
        self._socket = SocketServer(on_message=self._handle_message)
        self._active_notifications: dict[str, dict] = {}
        self._pending_queue: asyncio.Queue[dict] = asyncio.Queue()
        self._running = True

    async def _handle_message(self, msg: dict) -> None:
        """Handle a message from clawd-notify via the socket."""
        event = msg.get("event")
        session_id = msg.get("session_id", "")

        if event == "add":
            self._active_notifications[session_id] = msg
        elif event == "dismiss":
            self._active_notifications.pop(session_id, None)

        await self._pending_queue.put(msg)

    async def _replay_active(self) -> None:
        """Replay all active notifications after reconnect."""
        logger.info("Replaying %d active notifications", len(self._active_notifications))
        for msg in self._active_notifications.values():
            payload = daemon_message_to_ble_payload(msg)
            await self._ble.write_notification(payload)
            await asyncio.sleep(0.05)  # Small delay between writes

    def _write_pid(self) -> None:
        PID_PATH.parent.mkdir(parents=True, exist_ok=True)
        PID_PATH.write_text(str(os.getpid()))

    def _remove_pid(self) -> None:
        if PID_PATH.exists():
            PID_PATH.unlink()

    async def _shutdown(self) -> None:
        logger.info("Shutting down...")
        self._running = False
        self._shutdown_event.set()

        # Send clear to ESP32
        clear_payload = daemon_message_to_ble_payload({"event": "clear"})
        await self._ble.write_notification(clear_payload)
        await self._ble.disconnect()
        await self._socket.stop()
        self._remove_pid()

    async def _ble_sender(self) -> None:
        """Process pending messages and send them over BLE."""
        while self._running:
            try:
                msg = await asyncio.wait_for(self._pending_queue.get(), timeout=1.0)
            except asyncio.TimeoutError:
                continue
            await self._ble.ensure_connected()

            payload = daemon_message_to_ble_payload(msg)
            success = await self._ble.write_notification(payload)

            if not success:
                await self._ble.ensure_connected()
                await self._replay_active()

    async def run(self) -> None:
        """Main daemon loop."""
        logging.basicConfig(
            level=logging.INFO,
            format="%(asctime)s [%(name)s] %(levelname)s: %(message)s",
        )

        self._write_pid()
        self._shutdown_event = asyncio.Event()

        loop = asyncio.get_event_loop()
        for sig in (signal.SIGTERM, signal.SIGINT):
            loop.add_signal_handler(sig, lambda: asyncio.create_task(self._shutdown()))

        await self._socket.start()

        # Start BLE connection in background (non-blocking)
        ble_connect_task = asyncio.create_task(self._ble.connect())
        sender_task = asyncio.create_task(self._ble_sender())

        # Wait until shutdown is signaled
        await self._shutdown_event.wait()

        # Cancel background tasks
        sender_task.cancel()
        ble_connect_task.cancel()
        for task in (sender_task, ble_connect_task):
            try:
                await task
            except asyncio.CancelledError:
                pass

        self._remove_pid()


def main():
    daemon = ClawdDaemon()
    asyncio.run(daemon.run())


if __name__ == "__main__":
    main()
```

- [ ] **Step 3: Write test for daemon message handling**

```python
# host/tests/test_daemon.py
import asyncio
import pytest
from clawd_daemon.daemon import ClawdDaemon


@pytest.mark.asyncio
async def test_handle_add_tracks_notification():
    daemon = ClawdDaemon()
    msg = {"event": "add", "session_id": "s1", "project": "proj", "message": "hi"}
    await daemon._handle_message(msg)
    assert "s1" in daemon._active_notifications
    assert daemon._pending_queue.qsize() == 1


@pytest.mark.asyncio
async def test_handle_dismiss_removes_notification():
    daemon = ClawdDaemon()
    await daemon._handle_message(
        {"event": "add", "session_id": "s1", "project": "p", "message": "m"}
    )
    await daemon._handle_message({"event": "dismiss", "session_id": "s1"})
    assert "s1" not in daemon._active_notifications
    assert daemon._pending_queue.qsize() == 2


@pytest.mark.asyncio
async def test_dismiss_unknown_is_safe():
    daemon = ClawdDaemon()
    await daemon._handle_message({"event": "dismiss", "session_id": "nope"})
    assert daemon._pending_queue.qsize() == 1
```

- [ ] **Step 4: Run tests**

```bash
cd host
source .venv/bin/activate
python -m pytest tests/ -v
```

Expected: All tests pass (protocol + daemon tests).

- [ ] **Step 5: Commit**

```bash
git add host/clawd_daemon/socket_server.py host/clawd_daemon/daemon.py host/tests/test_daemon.py
git commit -m "feat: add socket server and daemon with notification tracking"
```

---

### Task 10: clawd-notify CLI Script

**Files:**
- Create: `host/clawd-notify`

- [ ] **Step 1: Write clawd-notify**

```python
#!/usr/bin/env python3
"""clawd-notify — Claude Code hook handler for Clawd notifications.

Reads hook payload from stdin, forwards to the Clawd daemon.
Starts the daemon if not running.
"""

import json
import os
import socket
import subprocess
import sys
import time
from pathlib import Path

# Ensure clawd_daemon package is importable
sys.path.insert(0, str(Path(__file__).parent))

CLAWD_DIR = Path.home() / ".clawd"
PID_PATH = CLAWD_DIR / "daemon.pid"
SOCKET_PATH = CLAWD_DIR / "sock"

# Path to the daemon module (relative to this script)
DAEMON_MODULE = Path(__file__).parent / "clawd_daemon" / "daemon.py"


def is_daemon_running() -> bool:
    """Check if the daemon is alive via PID file."""
    if not PID_PATH.exists():
        return False
    try:
        pid = int(PID_PATH.read_text().strip())
        os.kill(pid, 0)  # Check if process exists
        return True
    except (ValueError, ProcessLookupError, PermissionError, OSError):
        # Stale PID file
        PID_PATH.unlink(missing_ok=True)
        return False


def start_daemon() -> None:
    """Start the daemon in the background."""
    CLAWD_DIR.mkdir(parents=True, exist_ok=True)

    # Start daemon as a detached subprocess
    log_file = open(CLAWD_DIR / "daemon.log", "a")
    subprocess.Popen(
        [sys.executable, "-m", "clawd_daemon.daemon"],
        cwd=str(Path(__file__).parent),
        stdout=log_file,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    log_file.close()

    # Wait for socket to appear
    for _ in range(50):  # 5 seconds max
        if SOCKET_PATH.exists():
            return
        time.sleep(0.1)

    print("Warning: daemon may not have started", file=sys.stderr)


def send_to_daemon(payload: dict) -> None:
    """Send a JSON message to the daemon via Unix socket."""
    from clawd_daemon.protocol import hook_payload_to_daemon_message

    msg = hook_payload_to_daemon_message(payload)
    if msg is None:
        return  # Irrelevant hook event

    try:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.settimeout(3.0)
        sock.connect(str(SOCKET_PATH))
        sock.sendall(json.dumps(msg).encode("utf-8"))
        sock.close()
    except (ConnectionRefusedError, FileNotFoundError, socket.timeout) as e:
        print(f"Failed to send to daemon: {e}", file=sys.stderr)
        sys.exit(1)


def main():
    # Read hook payload from stdin
    try:
        raw = sys.stdin.read()
        if not raw.strip():
            sys.exit(0)
        payload = json.loads(raw)
    except json.JSONDecodeError as e:
        print(f"Invalid JSON from hook: {e}", file=sys.stderr)
        sys.exit(1)

    # Ensure daemon is running
    if not is_daemon_running():
        start_daemon()

    # Forward to daemon
    send_to_daemon(payload)


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Make executable**

```bash
chmod +x host/clawd-notify
```

- [ ] **Step 3: Commit**

```bash
git add host/clawd-notify
git commit -m "feat: add clawd-notify CLI hook handler"
```

---

### Task 11: Claude Code Hook Configuration

**Files:**
- Create: `host/install-hooks.sh` (helper to install hook config)

- [ ] **Step 1: Create install-hooks.sh**

```bash
#!/bin/bash
# install-hooks.sh — Installs Clawd notification hooks into Claude Code settings.
# Usage: ./install-hooks.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CLAWD_NOTIFY="$SCRIPT_DIR/clawd-notify"
SETTINGS_FILE="$HOME/.claude/settings.json"

if [ ! -f "$CLAWD_NOTIFY" ]; then
    echo "Error: clawd-notify not found at $CLAWD_NOTIFY"
    exit 1
fi

echo "Clawd notify path: $CLAWD_NOTIFY"
echo ""
echo "Add the following to $SETTINGS_FILE under the 'hooks' key:"
echo ""
cat <<EOF
{
  "hooks": {
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
}
EOF

echo ""
echo "NOTE: If you already have hooks configured, merge the above into your existing config."
echo "The hook config goes in ~/.claude/settings.json (NOT hooks.json)."
echo ""
echo "The 'matcher' field filters which notification types trigger the hook."
echo "If your Claude Code version doesn't support 'matcher', remove it —"
echo "clawd-notify already filters by notification_type in protocol.py."
```

- [ ] **Step 2: Make executable**

```bash
chmod +x host/install-hooks.sh
```

- [ ] **Step 3: Commit**

```bash
git add host/install-hooks.sh
git commit -m "feat: add hook installation helper script"
```

---

### Task 12: End-to-End Verification

No new files — this task verifies the full pipeline works.

- [ ] **Step 1: Flash firmware to ESP32-C6**

```bash
cd firmware
idf.py -p /dev/ttyACM0 flash monitor
```

Verify: LCD shows "[Clawd] zzz / Disconnected". BLE advertising as "Clawd" visible in scanner app.

- [ ] **Step 2: Start daemon manually**

```bash
cd host
source .venv/bin/activate
python -m clawd_daemon.daemon
```

Verify: Log shows "Scanning for Clawd device...", then "Connected to Clawd". ESP32 LCD changes to "[Clawd] :) / All clear!".

- [ ] **Step 3: Test adding a notification via socket**

In a second terminal:

```bash
echo '{"event":"add","session_id":"test-1","project":"my-project","message":"Waiting for input"}' | socat - UNIX-CONNECT:~/.clawd/sock
```

Verify: ESP32 LCD shows "[Clawd] !! / 1 waiting" with "my-project" in the list.

- [ ] **Step 4: Test dismissing a notification**

```bash
echo '{"event":"dismiss","session_id":"test-1"}' | socat - UNIX-CONNECT:~/.clawd/sock
```

Verify: ESP32 LCD returns to "[Clawd] :) / All clear!".

- [ ] **Step 5: Test clawd-notify script end-to-end**

```bash
echo '{"hook_event_name":"Notification","notification_type":"idle_prompt","session_id":"s2","cwd":"/Users/me/Projects/cool-app","message":"Claude is waiting"}' | ./clawd-notify
```

Verify: ESP32 LCD shows notification for "cool-app".

- [ ] **Step 6: Install hooks and test with real Claude Code**

```bash
./install-hooks.sh
```

Follow the instructions to add hooks to `~/.claude/settings.json`. Start a Claude Code session and verify notifications appear on the ESP32 LCD when Claude is waiting for input.

