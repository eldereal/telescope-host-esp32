#include "freertos/FreeRTOS.h"

void init_mount_encoder();

int32_t get_ra_angle_millis();
int32_t get_dec_angle_millis();
int32_t get_dec_mechnical_angle_millis();

void ra_pulse_freq_changed(int32_t raFreq);
void dec_pulse_freq_changed(int32_t decFreq);

void set_angles(int32_t ra_angle_millis, int32_t dec_angle_millis);