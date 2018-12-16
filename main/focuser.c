#include "focuser.h"
#include "esp_timer.h"
#include "util.h"
#include "driver/gpio.h"

int focuser_gpio_nums[4] = {
    CONFIG_GPIO_FOCUS_IN1,
    CONFIG_GPIO_FOCUS_IN2,
    CONFIG_GPIO_FOCUS_IN3,
    CONFIG_GPIO_FOCUS_IN4
};

bool focuser_step_config[][4] = {
    { 1, 0, 0, 0 },
    { 1, 1, 0, 0 },
    { 0, 1, 0, 0 },
    { 0, 1, 1, 0 },
    { 0, 0, 1, 0 },
    { 0, 0, 1, 1 },
    { 0, 0, 0, 1 },
    { 1, 0, 0, 1 }
};

int32_t focuser_step;
int32_t focuser_target_step;
bool focuser_is_moving;

void focuser_timer_listener(void* args);

esp_timer_handle_t focuser_timer;

esp_timer_create_args_t focuser_timer_args = {
    .dispatch_method = ESP_TIMER_TASK,
    .callback = focuser_timer_listener
};

void focuser_timer_listener(void* args) {
    if (focuser_step > focuser_target_step) {
        focuser_step --;
    } else if (focuser_step < focuser_target_step)  {
        focuser_step ++;
    } else {//reach target
        for (int i = 0; i < 4; i++) {            
            gpio_set_level(focuser_gpio_nums[i], 0);
        }
        focuser_is_moving = 0;
        esp_timer_stop(focuser_timer);
        return;
    }
    
    bool *output = focuser_step_config[focuser_step % 8];
    for (int i = 0; i < 4; i++) {            
        gpio_set_level(focuser_gpio_nums[i], output[i]);
    }
}

void focuser_init() {
    ESP_ERROR_CHECK(esp_timer_create(&focuser_timer_args, &focuser_timer));
    for (int i = 0; i < 4; i ++) {
        gpio_pad_select_gpio(focuser_gpio_nums[i]);
        gpio_set_direction(focuser_gpio_nums[i], GPIO_MODE_OUTPUT);
        gpio_set_level(focuser_gpio_nums[i], 0);
    }
    focuser_step = 0;
    focuser_target_step = 0;
    focuser_is_moving = 0;
}

uint16_t focuser_get_movement_nanos_per_step() {
    return CONFIG_FOCUS_MOVEMENT_MICRONS_PER_CYCLE * 1000 / CONFIG_FOCUS_STEPS_PER_CYCLE;
}

#define MAX_STEPS (CONFIG_FOCUS_TOTAL_MOVEMENT_MICRONS * CONFIG_FOCUS_STEPS_PER_CYCLE / CONFIG_FOCUS_MOVEMENT_MICRONS_PER_CYCLE)

uint32_t focuser_get_max_steps() {
    return MAX_STEPS;
}

#define STEP_INTERVAL_MICROSECONDS (1000000 / (CONFIG_FOCUS_MOVEMENT_SPEED_MICRONS_PER_SECOND * CONFIG_FOCUS_STEPS_PER_CYCLE / CONFIG_FOCUS_MOVEMENT_MICRONS_PER_CYCLE))

void focuser_move(int32_t steps) {
    if (steps > MAX_STEPS) steps = MAX_STEPS;
    if (steps < -MAX_STEPS) steps = -MAX_STEPS;
    focuser_target_step += steps;
    if (focuser_target_step > MAX_STEPS) focuser_target_step = MAX_STEPS;
    if (focuser_target_step < -MAX_STEPS) focuser_target_step = -MAX_STEPS;
    focuser_is_moving = 1;
    esp_timer_start_periodic(focuser_timer, STEP_INTERVAL_MICROSECONDS);
}

bool focuser_get_is_moving() {
    return focuser_is_moving;
}

void focuser_abort_move() {
    focuser_is_moving = 0;
    esp_timer_stop(focuser_timer);
    focuser_target_step = focuser_step;
}