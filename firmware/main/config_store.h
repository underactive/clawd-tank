// firmware/main/config_store.h
#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include <stdbool.h>
#include <stdint.h>

#define CONFIG_DEFAULT_BRIGHTNESS       230  /* 90 % of 8-bit PWM duty (255) */
#define CONFIG_DEFAULT_SLEEP_TIMEOUT    300  /* seconds */
#define CONFIG_DEFAULT_DISPLAY_FLIPPED  0    /* 0 = native, 1 = 180° rotated */

// Initialize config store — loads from NVS, or uses defaults.
// Must be called before display_init().
void config_store_init(void);

// Getters — return current in-memory values.
uint8_t  config_store_get_brightness(void);
uint32_t config_store_get_sleep_timeout_ms(void);  /* returns milliseconds */
bool     config_store_get_display_flipped(void);

// Setters — update in-memory value AND persist to NVS.
void config_store_set_brightness(uint8_t duty);
void config_store_set_sleep_timeout(uint16_t seconds);
void config_store_set_display_flipped(bool flipped);

// Serialize full config to JSON. Returns number of bytes written (excluding null).
// Output is null-terminated.
uint16_t config_store_serialize_json(char *buf, uint16_t buf_sz);

#endif // CONFIG_STORE_H
