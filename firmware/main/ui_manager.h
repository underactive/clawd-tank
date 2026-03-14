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

#ifdef SIMULATOR
// Get current animation info (frame count and ms per frame).
void ui_manager_get_anim_info(int *frame_count, int *frame_ms);

// Get current animation frame index.
int ui_manager_get_frame_idx(void);
#endif

#endif // UI_MANAGER_H
