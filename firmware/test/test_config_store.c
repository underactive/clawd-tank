// firmware/test/test_config_store.c
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

// Pull in stub headers that satisfy config_store.c's ESP-IDF includes.
// These stubs live in firmware/test/stubs/ and are on the include path
// via -I../test/stubs in the Makefile.
#include "nvs.h"

// --- NVS mock ---
// Types and ESP_OK/ESP_ERR_NVS_NOT_FOUND come from nvs.h stub above.

static uint8_t  mock_brightness = 0;
static uint16_t mock_sleep_timeout = 0;
static uint8_t  mock_display_flipped = 0;
static int mock_brightness_set = 0;
static int mock_sleep_timeout_set = 0;
static int mock_display_flipped_set = 0;

static void mock_nvs_reset(void) {
    mock_brightness = 0;
    mock_sleep_timeout = 0;
    mock_display_flipped = 0;
    mock_brightness_set = 0;
    mock_sleep_timeout_set = 0;
    mock_display_flipped_set = 0;
}

esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *handle) {
    (void)ns; (void)mode;
    *handle = 1;
    return ESP_OK;
}

esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *val) {
    (void)h;
    if (strcmp(key, "brightness") == 0 && mock_brightness_set) {
        *val = mock_brightness;
        return ESP_OK;
    }
    if (strcmp(key, "disp_flip") == 0 && mock_display_flipped_set) {
        *val = mock_display_flipped;
        return ESP_OK;
    }
    return ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t nvs_get_u16(nvs_handle_t h, const char *key, uint16_t *val) {
    (void)h;
    if (strcmp(key, "sleep_tmout") == 0 && mock_sleep_timeout_set) {
        *val = mock_sleep_timeout;
        return ESP_OK;
    }
    return ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t val) {
    (void)h;
    if (strcmp(key, "brightness") == 0) {
        mock_brightness = val;
        mock_brightness_set = 1;
    } else if (strcmp(key, "disp_flip") == 0) {
        mock_display_flipped = val;
        mock_display_flipped_set = 1;
    }
    return ESP_OK;
}

esp_err_t nvs_set_u16(nvs_handle_t h, const char *key, uint16_t val) {
    (void)h;
    if (strcmp(key, "sleep_tmout") == 0) {
        mock_sleep_timeout = val;
        mock_sleep_timeout_set = 1;
    }
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }

// Now include the implementation directly (compiles with our mocks)
#include "../main/config_store.c"

// --- Tests ---

static void test_defaults_when_nvs_empty(void) {
    mock_nvs_reset();
    config_store_init();
    assert(config_store_get_brightness() == CONFIG_DEFAULT_BRIGHTNESS);
    assert(config_store_get_sleep_timeout_ms() == CONFIG_DEFAULT_SLEEP_TIMEOUT * 1000);
    printf("  PASS: test_defaults_when_nvs_empty\n");
}

static void test_loads_from_nvs(void) {
    mock_nvs_reset();
    mock_brightness = 200;
    mock_brightness_set = 1;
    mock_sleep_timeout = 60;
    mock_sleep_timeout_set = 1;
    config_store_init();
    assert(config_store_get_brightness() == 200);
    assert(config_store_get_sleep_timeout_ms() == 60000);
    printf("  PASS: test_loads_from_nvs\n");
}

static void test_set_brightness_persists(void) {
    mock_nvs_reset();
    config_store_init();
    config_store_set_brightness(255);
    assert(config_store_get_brightness() == 255);
    assert(mock_brightness == 255);
    assert(mock_brightness_set == 1);
    printf("  PASS: test_set_brightness_persists\n");
}

static void test_set_sleep_timeout_persists(void) {
    mock_nvs_reset();
    config_store_init();
    config_store_set_sleep_timeout(120);
    assert(config_store_get_sleep_timeout_ms() == 120000);
    assert(mock_sleep_timeout == 120);
    assert(mock_sleep_timeout_set == 1);
    printf("  PASS: test_set_sleep_timeout_persists\n");
}

static void test_display_flipped_default_false(void) {
    mock_nvs_reset();
    config_store_init();
    assert(config_store_get_display_flipped() == false);
    printf("  PASS: test_display_flipped_default_false\n");
}

static void test_set_display_flipped_persists(void) {
    mock_nvs_reset();
    config_store_init();
    config_store_set_display_flipped(true);
    assert(config_store_get_display_flipped() == true);
    assert(mock_display_flipped == 1);
    assert(mock_display_flipped_set == 1);

    /* Toggling back writes 0 and clears the getter. */
    config_store_set_display_flipped(false);
    assert(config_store_get_display_flipped() == false);
    assert(mock_display_flipped == 0);
    printf("  PASS: test_set_display_flipped_persists\n");
}

static void test_display_flipped_loads_from_nvs(void) {
    mock_nvs_reset();
    mock_display_flipped = 1;
    mock_display_flipped_set = 1;
    config_store_init();
    assert(config_store_get_display_flipped() == true);
    printf("  PASS: test_display_flipped_loads_from_nvs\n");
}

static void test_serialize_json(void) {
    mock_nvs_reset();
    config_store_init();
    config_store_set_brightness(128);
    config_store_set_sleep_timeout(600);

    char buf[256];
    uint16_t len = config_store_serialize_json(buf, sizeof(buf));
    assert(len > 0);
    assert(len == strlen(buf));

    // Verify it contains expected fields
    assert(strstr(buf, "\"brightness\":128") != NULL);
    assert(strstr(buf, "\"sleep_timeout\":600") != NULL);
    assert(strstr(buf, "\"display_flipped\":0") != NULL);
    printf("  PASS: test_serialize_json\n");
}

static void test_serialize_json_includes_display_flipped(void) {
    mock_nvs_reset();
    config_store_init();
    config_store_set_display_flipped(true);

    char buf[256];
    uint16_t len = config_store_serialize_json(buf, sizeof(buf));
    assert(len > 0);
    assert(strstr(buf, "\"display_flipped\":1") != NULL);
    printf("  PASS: test_serialize_json_includes_display_flipped\n");
}

static void test_serialize_json_default_values(void) {
    mock_nvs_reset();
    config_store_init();

    char buf[256];
    uint16_t len = config_store_serialize_json(buf, sizeof(buf));
    assert(len > 0);
    char expected_brightness[32];
    snprintf(expected_brightness, sizeof(expected_brightness),
             "\"brightness\":%d", CONFIG_DEFAULT_BRIGHTNESS);
    char expected_timeout[32];
    snprintf(expected_timeout, sizeof(expected_timeout),
             "\"sleep_timeout\":%d", CONFIG_DEFAULT_SLEEP_TIMEOUT);
    assert(strstr(buf, expected_brightness) != NULL);
    assert(strstr(buf, expected_timeout) != NULL);
    printf("  PASS: test_serialize_json_default_values\n");
}

static void test_serialize_json_small_buffer(void) {
    mock_nvs_reset();
    config_store_init();

    char buf[10];  // Too small for full JSON
    uint16_t len = config_store_serialize_json(buf, sizeof(buf));
    // Should return 0 or truncated safely
    assert(len < sizeof(buf));
    printf("  PASS: test_serialize_json_small_buffer\n");
}

int main(void) {
    printf("Running config store tests...\n");
    test_defaults_when_nvs_empty();
    test_loads_from_nvs();
    test_set_brightness_persists();
    test_set_sleep_timeout_persists();
    test_display_flipped_default_false();
    test_set_display_flipped_persists();
    test_display_flipped_loads_from_nvs();
    test_serialize_json();
    test_serialize_json_includes_display_flipped();
    test_serialize_json_default_values();
    test_serialize_json_small_buffer();
    printf("All config store tests passed!\n");
    return 0;
}
