#include "rencoder.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "string.h"

static rencoder_t *gpio2enc[48];
static xQueueHandle gpio_evt_queue = NULL;

void interrupt(rencoder_t* self, gpio_num_t gpio);

static void IRAM_ATTR gpio_isr_handler(void* arg) {
    gpio_num_t gpio_num = (gpio_num_t)(int) arg;
    if(gpio2enc[gpio_num] != NULL)
        interrupt(gpio2enc[gpio_num], gpio_num);
}

esp_err_t rencoder_init() {
    bzero(gpio2enc, sizeof(gpio2enc));
    gpio_evt_queue = xQueueCreate(16, sizeof(uint32_t));
    return gpio_install_isr_service(0);
}

void interrupt(rencoder_t* self, gpio_num_t gpio) {
    if(self->working) {
        if (gpio == self->a && gpio_get_level(self->a)) {
            bool dir = gpio_get_level(self->b);
            if (dir != self->direction && self->direction_callback != NULL) {
                self->direction_callback(self, dir, self->direction_callback_args);
            }
            self->direction = dir;
        }
        int32_t next;
        int8_t diff;
        if(self->direction) {
            next = self->count + 1;
            diff = 1;
        } else {
            next = self->count - 1;
            diff = -1;
        }
        if (self->count_callback != NULL) {
            self->count_callback(self, next, diff, self->count_callback_args);
        }
        self->count = next;
    }
}

esp_err_t rencoder_start(rencoder_t *self, gpio_num_t a, gpio_num_t b, count_callback_f count_callback, direction_callback_f direction_callback) {
    self -> direction = true;
    self -> a = a;
    self -> b = b;
    self -> count_callback = count_callback;
    self -> direction_callback = direction_callback;
    gpio2enc[a] = self;
    gpio2enc[b] = self;
    self -> count = 0;
    self -> working = true;
    gpio_config_t conf;
    conf.intr_type = GPIO_INTR_ANYEDGE;
    conf.mode = GPIO_MODE_INPUT;
    conf.pin_bit_mask = (1 << a) | (1 << b);
    conf.pull_up_en = GPIO_PULLUP_ENABLE;
    conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&conf);
    
    esp_err_t err;
    err = gpio_isr_handler_add(a, gpio_isr_handler, (void*)a);
    if (err != ESP_OK) {
        return err;
    }
    return gpio_isr_handler_add(b, gpio_isr_handler, (void*)b);
}

esp_err_t rencoder_stop(rencoder_t *self) {
    gpio2enc[self -> a] = NULL;
    gpio2enc[self -> b] = NULL;
    esp_err_t err1, err2;
    err1 = gpio_isr_handler_remove(self -> a);
    err2 = gpio_isr_handler_remove(self -> b);
    if (err1 != ESP_OK) return err1;
    return err2;    
}

void rencoder_pause(rencoder_t *self){
    self -> working = false;
}

void rencoder_clear(rencoder_t *self) {
    self -> count = 0;
}

void rencoder_resume(rencoder_t *self) {
    self -> working = true;
}

bool rencoder_getdirection(rencoder_t *self) {
    return self -> direction;
}

int32_t rencoder_value(rencoder_t *self) {
    return self -> count;
}