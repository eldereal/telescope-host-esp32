#include "freertos/FreeRTOS.h"

void init_mount_encoder();

int32_t get_ra_pulses_raw();
int32_t get_dec_pulses_raw();
bool get_ra_direction();
bool get_dec_direction();
int32_t get_ra_pulses();
int32_t get_dec_pulses();
int32_t get_ra_angle_millis();
int32_t get_dec_angle_millis();
int32_t get_dec_mechnical_angle_millis();

void set_angles(int32_t ra_angle_millis, int32_t dec_angle_millis);