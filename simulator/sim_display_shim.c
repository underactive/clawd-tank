/* Simulator shim for firmware display functions.
 * Provides implementations of display_init() and display_set_brightness()
 * so that shared firmware sources (ui_manager.c) can link against them. */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

void display_set_brightness(uint8_t duty) {
    printf("[sim] display_set_brightness(%d)\n", duty);
}

void display_set_flipped(bool flipped) {
    printf("[sim] display_set_flipped(%d)\n", (int)flipped);
}
