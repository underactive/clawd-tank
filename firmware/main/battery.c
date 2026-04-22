// firmware/main/battery.c
//
// LiPo battery ADC sampling for boards with a voltage divider to GPIO 9
// (fnk0104). Reads once every 5 seconds, applies EMA smoothing, and exposes
// the result via battery_poll().
//
// Calibration: prefer curve fitting (the ESP32-S3 supports it), fall back to
// line fitting if creation fails. Skipping calibration produces 5-10 %
// inaccurate readings because the raw ADC<->mV mapping is nonlinear and
// varies by die.

#include "battery.h"
#include "board_config.h"

#if BOARD_HAS_BATTERY

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "battery";

#define SAMPLE_PERIOD_US  (5 * 1000 * 1000)  /* 5 s */
#define EMA_ALPHA_PCT     15                  /* 0.15 = 15 % */

static adc_oneshot_unit_handle_t s_adc_unit;
static adc_cali_handle_t s_adc_cali;
static adc_channel_t s_adc_channel;
static bool s_has_reading;
static float s_ema_mv;
static uint8_t s_cached_pct;
static bool s_cached_charging;
static uint8_t s_last_reported_pct = 255;  /* sentinel — forces first poll to return true */
static bool s_last_reported_charging;

static uint8_t mv_to_pct(int mv)
{
    if (mv >= BOARD_BATTERY_FULL_MV) return 100;
    if (mv <= BOARD_BATTERY_EMPTY_MV) return 0;
    int span = BOARD_BATTERY_FULL_MV - BOARD_BATTERY_EMPTY_MV;
    int off  = mv - BOARD_BATTERY_EMPTY_MV;
    return (uint8_t)((off * 100 + span / 2) / span);
}

static void sample_cb(void *arg)
{
    (void)arg;
    int raw = 0;
    if (adc_oneshot_read(s_adc_unit, s_adc_channel, &raw) != ESP_OK) return;

    int mv = 0;
    if (adc_cali_raw_to_voltage(s_adc_cali, raw, &mv) != ESP_OK) return;

    /* Apply the 2x voltage divider — the ADC sees half the battery voltage. */
    float pack_mv = (float)mv * BOARD_BATTERY_DIVIDER;

    if (!s_has_reading) {
        s_ema_mv = pack_mv;
        s_has_reading = true;
    } else {
        /* EMA: new = alpha * raw + (1 - alpha) * old */
        s_ema_mv = (pack_mv * EMA_ALPHA_PCT + s_ema_mv * (100 - EMA_ALPHA_PCT)) / 100.0f;
    }

    s_cached_pct = mv_to_pct((int)s_ema_mv);
    s_cached_charging = ((int)s_ema_mv > BOARD_BATTERY_CHARGING_MV);

    ESP_LOGD(TAG, "raw=%d mv=%d pack=%.0f ema=%.0f pct=%u%s",
             raw, mv, pack_mv, s_ema_mv, s_cached_pct,
             s_cached_charging ? " [charging]" : "");
}

static bool init_calibration(adc_unit_t unit, adc_channel_t chan, adc_atten_t atten)
{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cfg = {
        .unit_id  = unit,
        .chan     = chan,
        .atten    = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cfg, &s_adc_cali) == ESP_OK) {
        ESP_LOGI(TAG, "ADC calibration: curve fitting");
        return true;
    }
#endif
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cfg = {
        .unit_id  = unit,
        .atten    = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_line_fitting(&cfg, &s_adc_cali) == ESP_OK) {
        ESP_LOGI(TAG, "ADC calibration: line fitting");
        return true;
    }
#endif
    ESP_LOGW(TAG, "No ADC calibration available — readings will be approximate");
    return false;
}

void battery_init(void)
{
    /* GPIO 9 on ESP32-S3 maps to ADC1_CH8. Hardcoded here because the
     * gpio-to-adc-channel mapping is chip-specific and lives in the ESP-IDF
     * headers as a lookup; we rely on the build target being S3. */
    adc_unit_t unit = ADC_UNIT_1;
    s_adc_channel  = ADC_CHANNEL_8;

    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = unit,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    if (adc_oneshot_new_unit(&init_cfg, &s_adc_unit) != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed");
        return;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,     /* 0–~3.1 V range covers 1.5..2.1 V at the divided node */
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_oneshot_config_channel(s_adc_unit, s_adc_channel, &chan_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel failed");
        return;
    }

    init_calibration(unit, s_adc_channel, ADC_ATTEN_DB_12);

    const esp_timer_create_args_t args = {
        .callback = &sample_cb,
        .name = "battery",
    };
    esp_timer_handle_t timer;
    if (esp_timer_create(&args, &timer) != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create failed");
        return;
    }

    /* Fire once immediately so the HUD shows a plausible reading before the
     * first periodic tick. esp_timer doesn't expose a "start + fire now"
     * mode, so we run one sample inline then start the periodic timer. */
    sample_cb(NULL);
    esp_timer_start_periodic(timer, SAMPLE_PERIOD_US);

    ESP_LOGI(TAG, "Battery monitor initialized on GPIO %d", BOARD_BATTERY_ADC_GPIO);
}

bool battery_poll(uint8_t *pct, bool *charging)
{
    if (!s_has_reading) return false;

    bool changed = (s_cached_pct != s_last_reported_pct) ||
                   (s_cached_charging != s_last_reported_charging);
    if (!changed) return false;

    *pct      = s_cached_pct;
    *charging = s_cached_charging;
    s_last_reported_pct      = s_cached_pct;
    s_last_reported_charging = s_cached_charging;
    return true;
}

#else  /* !BOARD_HAS_BATTERY */

void battery_init(void) {}
bool battery_poll(uint8_t *pct, bool *charging) { (void)pct; (void)charging; return false; }

#endif
