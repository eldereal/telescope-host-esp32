#ifndef __UTIL_H
#define __UTIL_H

#include "esp_log.h"

#define LOGI(tag, format, ...) ESP_LOGI(tag, "[%lld] "format, esp_timer_get_time(), ##__VA_ARGS__)
#define LOGE(tag, format, ...) ESP_LOGE(tag, "[%lld] "format, esp_timer_get_time(), ##__VA_ARGS__)
#define ESP_ERROR_CHECK_ALLOW_INVALID_STATE(x) do { \
    esp_err_t err = (x);                            \
    if (err != ESP_ERR_INVALID_STATE) {             \
        ESP_ERROR_CHECK(err);                       \
    }                                               \
} while(0);
#define SLEEP(ms) vTaskDelay(ms / portTICK_PERIOD_MS)
#define LEN(arr) (sizeof(arr) / sizeof(arr[0]))

uint64_t currentTimeMillis();

#endif
