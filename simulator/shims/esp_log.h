#ifndef SIM_ESP_LOG_H
#define SIM_ESP_LOG_H

#include <stdio.h>

#define ESP_LOGI(tag, fmt, ...) printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) /* no-op */
#define ESP_LOGV(tag, fmt, ...) /* no-op */

static inline const char *esp_err_to_name(int err) { (void)err; return "SIM_ERR"; }

#endif
