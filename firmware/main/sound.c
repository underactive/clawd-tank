// firmware/main/sound.c
#include "sound.h"

#if BOARD_HAS_AUDIO

#include "i2c_bus.h"
#include "es8311.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include <string.h>

static const char *TAG = "sound";

/* --- Clip table: one entry per sound_id_t. Raw 16-bit mono PCM at
 * BOARD_AUDIO_SAMPLE_RATE. The .pcm file is linked into the binary via
 * EMBED_FILES in CMakeLists.txt, exposing _start / _end symbols. */
extern const uint8_t keyboard_type_pcm_start[]      asm("_binary_keyboard_type_pcm_start");
extern const uint8_t keyboard_type_pcm_end[]        asm("_binary_keyboard_type_pcm_end");
extern const uint8_t notification_click_pcm_start[] asm("_binary_notification_click_pcm_start");
extern const uint8_t notification_click_pcm_end[]   asm("_binary_notification_click_pcm_end");
extern const uint8_t hammering_pcm_start[]          asm("_binary_hammering_pcm_start");
extern const uint8_t hammering_pcm_end[]            asm("_binary_hammering_pcm_end");
extern const uint8_t thinking_robot_pcm_start[]     asm("_binary_thinking_robot_pcm_start");
extern const uint8_t thinking_robot_pcm_end[]       asm("_binary_thinking_robot_pcm_end");
extern const uint8_t searching_pcm_start[]          asm("_binary_searching_pcm_start");
extern const uint8_t searching_pcm_end[]            asm("_binary_searching_pcm_end");

typedef struct {
    const uint8_t *data;
    const uint8_t *end;
} sound_clip_t;

static sound_clip_t s_clips[SOUND_COUNT];

/* --- I2S + codec state */
#define CHUNK_SAMPLES     512
#define DMA_FRAME_NUM     512
#define DMA_DESC_NUM      6
#define PREFILL_CHUNKS    4     /* per sound_update() call */
#define AMP_SETTLE_MS     10

static i2s_chan_handle_t s_tx_chan = NULL;
static es8311_handle_t   s_codec   = NULL;

/* Playback cursor (edge-triggered from sound_play). */
static bool           s_playing     = false;
static const uint8_t *s_cur         = NULL;
static const uint8_t *s_end         = NULL;

/* Stereo scratch buffer — mono source duplicated to L+R. Static so we don't
 * allocate per chunk; one clip plays at a time so no contention. */
static int16_t s_stereo_buf[CHUNK_SAMPLES * 2];

static inline uint32_t amp_on_level(void)  { return BOARD_AUDIO_AMP_ACTIVE_LOW ? 0 : 1; }
static inline uint32_t amp_off_level(void) { return BOARD_AUDIO_AMP_ACTIVE_LOW ? 1 : 0; }

static void amp_set(bool on)
{
    gpio_set_level(BOARD_AUDIO_AMP_ENABLE_GPIO, on ? amp_on_level() : amp_off_level());
}

static esp_err_t init_amp_gpio(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BOARD_AUDIO_AMP_ENABLE_GPIO,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "amp gpio_config failed");
    amp_set(false);
    return ESP_OK;
}

static esp_err_t init_i2s(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num  = DMA_DESC_NUM;
    chan_cfg.dma_frame_num = DMA_FRAME_NUM;
    chan_cfg.auto_clear    = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx_chan, NULL), TAG, "i2s_new_channel");

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(BOARD_AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                       I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BOARD_AUDIO_I2S_MCK,
            .bclk = BOARD_AUDIO_I2S_BCK,
            .ws   = BOARD_AUDIO_I2S_WS,
            .dout = BOARD_AUDIO_I2S_DOUT,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = 0, .bclk_inv = 0, .ws_inv = 0 },
        },
    };
    /* MCLK must be an integer multiple of sample rate — 256x covers ES8311's
     * PLL input range at 24 kHz (6.144 MHz) and matches the codec's clk cfg. */
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx_chan, &std_cfg), TAG, "i2s_channel_init_std_mode");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_chan), TAG, "i2s_channel_enable");
    return ESP_OK;
}

static esp_err_t init_codec(void)
{
    i2c_master_bus_handle_t bus = i2c_bus_get();
    if (!bus) {
        ESP_LOGE(TAG, "i2c_bus_get returned NULL — call touch_init() first or check board config");
        return ESP_FAIL;
    }

    s_codec = es8311_create(bus, BOARD_AUDIO_CODEC_I2C_ADDR);
    if (!s_codec) {
        ESP_LOGE(TAG, "es8311_create failed");
        return ESP_FAIL;
    }

    es8311_clock_config_t clk = {
        .mclk_inverted      = false,
        .sclk_inverted      = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency     = BOARD_AUDIO_SAMPLE_RATE * BOARD_AUDIO_MCLK_MULT,
        .sample_frequency   = BOARD_AUDIO_SAMPLE_RATE,
    };
    ESP_RETURN_ON_ERROR(es8311_init(s_codec, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16),
                        TAG, "es8311_init");
    ESP_RETURN_ON_ERROR(es8311_voice_volume_set(s_codec, BOARD_AUDIO_VOLUME_PCT, NULL),
                        TAG, "es8311_voice_volume_set");
    ESP_RETURN_ON_ERROR(es8311_voice_mute(s_codec, false), TAG, "es8311_voice_mute");
    return ESP_OK;
}

void sound_init(void)
{
    if (s_tx_chan) return;  /* idempotent */

    if (init_amp_gpio() != ESP_OK) { ESP_LOGE(TAG, "sound_init: amp_gpio failed"); return; }
    if (init_i2s() != ESP_OK)      { ESP_LOGE(TAG, "sound_init: i2s failed");      return; }
    if (init_codec() != ESP_OK) {
        ESP_LOGE(TAG, "sound_init: codec failed — tearing down I2S");
        i2s_channel_disable(s_tx_chan);
        i2s_del_channel(s_tx_chan);
        s_tx_chan = NULL;
        return;
    }

    s_clips[SOUND_KEYBOARD_TYPE] = (sound_clip_t){
        .data = keyboard_type_pcm_start,
        .end  = keyboard_type_pcm_end,
    };
    s_clips[SOUND_NOTIFICATION_CLICK] = (sound_clip_t){
        .data = notification_click_pcm_start,
        .end  = notification_click_pcm_end,
    };
    s_clips[SOUND_BUILDING] = (sound_clip_t){
        .data = hammering_pcm_start,
        .end  = hammering_pcm_end,
    };
    s_clips[SOUND_THINKING] = (sound_clip_t){
        .data = thinking_robot_pcm_start,
        .end  = thinking_robot_pcm_end,
    };
    s_clips[SOUND_DEBUGGER] = (sound_clip_t){
        .data = searching_pcm_start,
        .end  = searching_pcm_end,
    };

    ESP_LOGI(TAG, "Audio ready (ES8311 @ 0x%02X, %d Hz, vol=%d%%)",
             BOARD_AUDIO_CODEC_I2C_ADDR, BOARD_AUDIO_SAMPLE_RATE, BOARD_AUDIO_VOLUME_PCT);
}

bool sound_is_playing(void) { return s_playing; }

void sound_play(sound_id_t id)
{
    if (!s_tx_chan) { ESP_LOGW(TAG, "sound_play(%d): ignored — not initialized", id); return; }
    if (id >= SOUND_COUNT) { ESP_LOGW(TAG, "sound_play: bad id %d", id); return; }
    if (s_playing) { ESP_LOGD(TAG, "sound_play(%d): already playing, skipping", id); return; }

    const sound_clip_t *c = &s_clips[id];
    if (!c->data || !c->end || c->end <= c->data) {
        ESP_LOGW(TAG, "sound_play(%d): empty clip (data=%p end=%p)", id, c->data, c->end);
        return;
    }

    s_cur     = c->data;
    s_end     = c->end;
    if (((s_end - s_cur) & 1) != 0) s_end -= 1;
    s_playing = true;

    ESP_LOGI(TAG, "sound_play(%d): %d bytes", id, (int)(s_end - s_cur));
    amp_set(true);
    vTaskDelay(pdMS_TO_TICKS(AMP_SETTLE_MS));
}

static void end_clip(void)
{
    s_playing = false;
    s_cur     = NULL;
    s_end     = NULL;
    amp_set(false);
}

void sound_update(void)
{
    if (!s_playing || !s_tx_chan) return;

    for (int attempt = 0; attempt < PREFILL_CHUNKS && s_playing; attempt++) {
        size_t remaining_bytes = (size_t)(s_end - s_cur);
        if (remaining_bytes == 0) { end_clip(); return; }

        size_t samples = remaining_bytes / 2;
        if (samples > CHUNK_SAMPLES) samples = CHUNK_SAMPLES;

        /* Mono → stereo: duplicate each sample into L and R. Reads the raw
         * PCM bytes little-endian (host ordering on ESP32 matches the asset). */
        for (size_t i = 0; i < samples; i++) {
            int16_t s = (int16_t)((uint16_t)s_cur[2*i] | ((uint16_t)s_cur[2*i + 1] << 8));
            s_stereo_buf[2*i]     = s;
            s_stereo_buf[2*i + 1] = s;
        }

        size_t bytes_to_write = samples * 2 * sizeof(int16_t);
        size_t bytes_written  = 0;
        esp_err_t err = i2s_channel_write(s_tx_chan, s_stereo_buf, bytes_to_write, &bytes_written, 0);
        if (err == ESP_ERR_TIMEOUT || bytes_written == 0) {
            /* DMA is full; back off and try on the next tick. */
            return;
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "i2s_channel_write: %s", esp_err_to_name(err));
            end_clip();
            return;
        }

        size_t samples_written = bytes_written / (2 * sizeof(int16_t));
        s_cur += samples_written * 2;  /* consumed mono source */
        if (s_cur >= s_end) { end_clip(); return; }
    }
}

#else /* !BOARD_HAS_AUDIO */

void sound_init(void) {}
void sound_play(sound_id_t id) { (void)id; }
void sound_update(void) {}
bool sound_is_playing(void) { return false; }

#endif
