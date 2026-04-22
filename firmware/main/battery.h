#pragma once
#include <stdint.h>
#include <stdbool.h>

// Start periodic ADC sampling of the LiPo battery. No-op on boards without a
// battery circuit (BOARD_HAS_BATTERY=0).
void battery_init(void);

// Retrieve the most recent reading.
//   *pct         — 0..100, EMA-smoothed percentage
//   *charging    — true if the pack is being charged (voltage high under load)
// Returns true if a reading is available OR changed meaningfully since the
// last call (pct delta >= 1 or charging state flipped). ui_manager polls this
// each tick; when it returns true we forward to scene_set_battery().
bool battery_poll(uint8_t *pct, bool *charging);
