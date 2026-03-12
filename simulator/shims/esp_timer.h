/* Simulator shim — esp_timer stubs */
#pragma once
#include <stdint.h>

typedef int esp_err_t;
typedef void *esp_timer_handle_t;

typedef struct {
    void (*callback)(void *arg);
    const char *name;
} esp_timer_create_args_t;

static inline esp_err_t esp_timer_create(const esp_timer_create_args_t *a, esp_timer_handle_t *h)
{ (void)a; *h = (void *)1; return 0; }
static inline esp_err_t esp_timer_start_periodic(esp_timer_handle_t h, uint64_t period_us)
{ (void)h; (void)period_us; return 0; }
static inline esp_err_t esp_timer_stop(esp_timer_handle_t h)
{ (void)h; return 0; }
