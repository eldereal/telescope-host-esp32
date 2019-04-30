#include "mount_encoder.h"
#include "util.h"
#include "sdkconfig.h"
#include "rencoder.h"
#include "astro.h"
#include "telescope.h"

uint64_t encoder_reset_time;
int32_t reset_ra_angle_millis, reset_dec_angle_millis;

#define TAG "MOUNT_ENCODER"

int64_t raPulses;
uint64_t raFreqSyncTime;
int32_t raFreq;

int64_t decPulses;
uint64_t decFreqSyncTime;
int32_t decFreq;

void init_mount_encoder(){
    encoder_reset_time = currentTimeMillis();
    reset_ra_angle_millis = 0;
    reset_dec_angle_millis = 0;
    raPulses = 0;
    decPulses = 0;
    raFreq = 0;
    decFreq = 0;
    raFreqSyncTime = currentTimeMillis();;
    decFreqSyncTime = currentTimeMillis();;
}

void ra_pulse_freq_changed(int32_t newRaFreq) {
    uint64_t currTime = currentTimeMillis();
    raPulses += (currTime - raFreqSyncTime) * raFreq / 1000;
    raFreqSyncTime = currTime;
    raFreq = newRaFreq;
}
void dec_pulse_freq_changed(int32_t newDecFreq) {
    uint64_t currTime = currentTimeMillis();
    decPulses += (currTime - decFreqSyncTime) * decFreq / 1000;
    decFreqSyncTime = currTime;
    decFreq = newDecFreq;
}

void reset_ra_actual_pulses(){
    raPulses = 0;
    raFreqSyncTime = currentTimeMillis();
}

void reset_dec_actual_pulses(){
    decPulses = 0;
    decFreqSyncTime = currentTimeMillis();
}

int64_t get_ra_actual_pulses(){
    uint64_t currTime = currentTimeMillis();
    return raPulses + (currTime - raFreqSyncTime) * raFreq / 1000;
}

int64_t get_dec_actual_pulses(){
    uint64_t currTime = currentTimeMillis();
    return decPulses + (currTime - decFreqSyncTime) * decFreq / 1000;
}

double ra_pulse_ratio = ((double) SIDEREAL_DAY_MILLIS) / (CONFIG_RA_CYCLE_STEPS * CONFIG_RA_RESOLUTION * CONFIG_RA_GEAR_RATIO);
double dec_pulse_ratio = ((double) DAY_MILLIS) / (CONFIG_DEC_CYCLE_STEPS * CONFIG_DEC_RESOLUTION * CONFIG_DEC_GEAR_RATIO);
double ra_time_ratio = ((double) DAY_MILLIS / (double) SIDEREAL_DAY_MILLIS);

int32_t get_ra_angle_millis() {
    double time_offset_millis = currentTimeMillis() - encoder_reset_time;
    double ra_moved_millis = (double)(ra_pulse_ratio * get_ra_actual_pulses());
    return (int32_t)(ra_time_ratio * (reset_ra_angle_millis + time_offset_millis - ra_moved_millis));
}

int32_t get_dec_angle_millis() {
    return decMecMillis2decMillis(get_dec_mechnical_angle_millis(), NULL);
}

int32_t get_dec_mechnical_angle_millis() {
    int32_t dec_moved_millis = (int32_t)(dec_pulse_ratio * get_dec_actual_pulses());
    return reset_dec_angle_millis + dec_moved_millis;
}

void set_angles(int32_t ra_angle_day_millis, int32_t dec_angle_day_millis) {
    encoder_reset_time = currentTimeMillis();
    int32_t ra_angle_sidereal_millis = (int32_t)((double)ra_angle_day_millis / ra_time_ratio);
    reset_ra_angle_millis = ra_angle_sidereal_millis;
    reset_dec_angle_millis = decMillis2decMecMillis(dec_angle_day_millis);
    reset_ra_actual_pulses();
    reset_dec_actual_pulses();
}