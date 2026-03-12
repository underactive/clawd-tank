// firmware/main/rgb_led.c
#include "rgb_led.h"
#include "led_strip.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "rgb_led";

#define RGB_LED_GPIO    8
#define FADE_STEP_MS    30   /* timer period for fade-out */

static led_strip_handle_t s_strip = NULL;

/* Flash state */
static esp_timer_handle_t s_fade_timer = NULL;
static uint8_t s_target_r, s_target_g, s_target_b;
static int s_fade_steps_left;
static int s_fade_total_steps;

static void apply_color(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_strip) return;
    led_strip_set_pixel(s_strip, 0, r, g, b);
    led_strip_refresh(s_strip);
}

static void fade_timer_cb(void *arg)
{
    (void)arg;
    s_fade_steps_left--;

    if (s_fade_steps_left <= 0) {
        apply_color(0, 0, 0);
        esp_timer_stop(s_fade_timer);
        return;
    }

    /* Linear fade out */
    float ratio = (float)s_fade_steps_left / (float)s_fade_total_steps;
    uint8_t r = (uint8_t)(s_target_r * ratio);
    uint8_t g = (uint8_t)(s_target_g * ratio);
    uint8_t b = (uint8_t)(s_target_b * ratio);
    apply_color(r, g, b);
}

void rgb_led_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = RGB_LED_GPIO,
        .max_leds = 1,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, /* 10 MHz */
        .flags.with_dma = false,
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init LED strip: %s", esp_err_to_name(err));
        return;
    }

    /* Start dark */
    led_strip_clear(s_strip);

    /* Create fade timer (one-shot repeated manually) */
    esp_timer_create_args_t timer_args = {
        .callback = fade_timer_cb,
        .name = "rgb_fade",
    };
    esp_timer_create(&timer_args, &s_fade_timer);

    ESP_LOGI(TAG, "RGB LED initialized on GPIO%d", RGB_LED_GPIO);
}

void rgb_led_set(uint8_t r, uint8_t g, uint8_t b)
{
    /* Stop any running flash */
    if (s_fade_timer) {
        esp_timer_stop(s_fade_timer);
    }
    apply_color(r, g, b);
}

void rgb_led_flash(uint8_t r, uint8_t g, uint8_t b, int duration_ms)
{
    if (!s_strip || !s_fade_timer) return;

    /* Stop previous flash if running */
    esp_timer_stop(s_fade_timer);

    s_target_r = r;
    s_target_g = g;
    s_target_b = b;
    s_fade_total_steps = duration_ms / FADE_STEP_MS;
    if (s_fade_total_steps < 2) s_fade_total_steps = 2;
    s_fade_steps_left = s_fade_total_steps;

    /* Start at full brightness */
    apply_color(r, g, b);

    /* Start periodic fade timer */
    esp_timer_start_periodic(s_fade_timer, FADE_STEP_MS * 1000); /* µs */
}
