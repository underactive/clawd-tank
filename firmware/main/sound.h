// firmware/main/sound.h
//
// Speaker playback via I2S + ES8311 codec + AP_ENABLE-gated amp, specific to
// the Freenove ESP32-S3 2.8" (fnk0104). Only triggered clip is the keyboard-
// typing effect; the module is shaped to allow more clips later.
//
// Lifecycle:
//   1) i2c_bus_get()   — touch.c already calls this; must have run before us
//   2) sound_init()    — brings up I2S, codec, amp gate (amp held OFF)
//   3) sound_play(id)  — edge-triggered; ignored if a clip is already playing
//   4) sound_update()  — call from the UI tick; streams PCM to DMA, no-op idle
//
// Guarded by BOARD_HAS_AUDIO — zero footprint on the C6 / simulator builds.

#pragma once

#include "board_config.h"
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    SOUND_KEYBOARD_TYPE = 0,
    SOUND_NOTIFICATION_CLICK,
    SOUND_BUILDING,
    SOUND_THINKING,
    SOUND_DEBUGGER,
    SOUND_COUNT
} sound_id_t;

void sound_init(void);
void sound_play(sound_id_t id);
void sound_update(void);
bool sound_is_playing(void);
