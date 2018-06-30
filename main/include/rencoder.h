#ifndef __RENCODER_H
#define __RENCODER_H
#include "driver/gpio.h"
#include "esp_err.h"

esp_err_t rencoder_init();

struct rencoder;

typedef void (*count_callback_f)(struct rencoder* target, int32_t next_count, int8_t difference, void* args);
typedef void (*direction_callback_f)(struct rencoder* target, bool next_direction, void* args);

typedef struct rencoder {
    gpio_num_t a, b;
    int32_t count;
    bool direction;
    bool working;
    count_callback_f count_callback;
    void *count_callback_args;
    direction_callback_f direction_callback;
    void *direction_callback_args;
} rencoder_t;

esp_err_t rencoder_start(rencoder_t *rencoder, gpio_num_t a, gpio_num_t b, count_callback_f count_callback, direction_callback_f direction_callback);
esp_err_t rencoder_stop(rencoder_t *rencoder);
void rencoder_pause(rencoder_t *rencoder);
void rencoder_clear(rencoder_t *rencoder);
void rencoder_resume(rencoder_t *rencoder);
bool rencoder_getdirection(rencoder_t *rencoder);
int32_t rencoder_value(rencoder_t *rencoder);

#endif