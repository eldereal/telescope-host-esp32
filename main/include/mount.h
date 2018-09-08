#ifndef __MOUNT_H
#define __MOUNT_H
#include "esp_err.h"

#define RA_CYCLE_MAX 30
#define RA_CYCLE_MIN 0.01
#define DEC_CYCLE_MAX 30
#define DEC_CYCLE_MIN 0.01
#define RA_SPEED_MAX 450000
#define RA_SPEED_MIN 150
#define DEC_SPEED_MAX 450000
#define DEC_SPEED_MIN 150

esp_err_t init_mount();

void set_ra_cycles_per_sidereal_day(double raCyclesPerSiderealDay);
void set_dec_cycles_per_day(double decCyclesPerDay);

double get_ra_cycles_per_sidereal_day();
double get_dec_cycles_per_day();

void set_mount_time_ratio_persist(double ratio);
double get_mount_time_ratio();

#endif