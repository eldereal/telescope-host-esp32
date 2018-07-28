#ifndef __SLEW_H
#define __SLEW_H

#include "freertos/FreeRTOS.h"
#include "esp_err.h"

typedef void (*slew_set_motor_speed_callback)(double raCyclesPerSiderealDay, double decCyclesPerDay);

esp_err_t init_slew(slew_set_motor_speed_callback callback);
bool is_slewing();
void abort_slew();
void slew_to_coordinates(int32_t raMillis, int32_t decMillis);
double get_slew_progress();
uint32_t get_slew_time_to_go_millis();
#endif