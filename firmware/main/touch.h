#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Initialize the FT6336G capacitive touch controller on the fnk0104 board.
// A dedicated task polls the panel at ~50 Hz; each touch-down event with
// debounce applied posts a BLE_EVT_NOTIF_CLEAR to evt_queue, matching the
// behavior of the BOOT button on the Waveshare C6 board.
//
// No-op on boards without touch (compiled out via BOARD_HAS_TOUCH).
void touch_init(QueueHandle_t evt_queue);
