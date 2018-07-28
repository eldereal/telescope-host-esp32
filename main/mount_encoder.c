#include "mount_encoder.h"
#include "util.h"
#include "sdkconfig.h"
#include "rencoder.h"
#include "astro.h"

uint64_t encoder_reset_time;
int32_t reset_ra_angle_millis, reset_dec_angle_millis;
rencoder_t ra_encoder, dec_encoder;
int32_t ra_pulses, dec_pulses;
int8_t ra_is_clearing_backlash, dec_is_clearing_backlash;
int32_t ra_backlash_pulses, dec_backlash_pulses;
int32_t ra_actual_pulses, dec_actual_pulses;

#define TAG "MOUNT_ENCODER"

#ifndef CONFIG_RA_REVERSE_RENCODER
#define CONFIG_RA_REVERSE_RENCODER false
#endif

#ifndef CONFIG_DEC_REVERSE_RENCODER
#define CONFIG_DEC_REVERSE_RENCODER false
#endif

void ra_encoder_dir_callback(rencoder_t* target, bool dir, void* args);
void ra_encoder_pul_callback(rencoder_t* target, int32_t pul, int8_t diff, void* args);
void dec_encoder_dir_callback(rencoder_t* target, bool dir, void* args);
void dec_encoder_pul_callback(rencoder_t* target, int32_t pul, int8_t diff, void* args);

void init_mount(){
    ESP_ERROR_CHECK_ALLOW_INVALID_STATE(rencoder_init());
    ESP_ERROR_CHECK(rencoder_start(&ra_encoder, CONFIG_GPIO_RA_RENCODER_A, CONFIG_GPIO_RA_RENCODER_B, ra_encoder_pul_callback, ra_encoder_dir_callback, CONFIG_RA_REVERSE_RENCODER));
    ESP_ERROR_CHECK(rencoder_start(&dec_encoder, CONFIG_GPIO_DEC_RENCODER_A, CONFIG_GPIO_DEC_RENCODER_B, dec_encoder_pul_callback, dec_encoder_dir_callback, CONFIG_DEC_REVERSE_RENCODER));
    encoder_reset_time = currentTimeMillis();
    ra_pulses = 0;
    dec_pulses = 0;
    ra_is_clearing_backlash = false;
    dec_is_clearing_backlash = false;
    ra_actual_pulses = 0;
    dec_actual_pulses = 0;
    reset_ra_angle_millis = 0;
    reset_dec_angle_millis = 0;
}

int32_t get_ra_pulses_raw() {
    return rencoder_value(&ra_encoder);
}

int32_t get_dec_pulses_raw() {
    return rencoder_value(&dec_encoder);
}

bool get_ra_direction() {
    return rencoder_getdirection(&ra_encoder);
}

bool get_dec_direction() {
    return rencoder_getdirection(&dec_encoder);
}

int32_t get_ra_pulses() {
    return ra_actual_pulses;
}

int32_t get_dec_pulses() {
    return dec_actual_pulses;
}

//RA_BACKLASH_PULSES
void ra_encoder_dir_callback(rencoder_t* target, bool dir, void* args) {
    if (dir) {//to positive
        if (ra_is_clearing_backlash == 1) { 
            //already clearing positive clearing, do nothing
        } else if (ra_is_clearing_backlash == -1) { //negative clearing
            //covert to positive clearing
            // LOGI(TAG, "negative clearing backlash revert to positive");
            ra_is_clearing_backlash = 1;
            ra_backlash_pulses = ra_backlash_pulses - CONFIG_RA_BACKLASH_PULSES; //expected_pulses_after_negative_clearing
        } else { //not clearing
            // LOGI(TAG, "positive clearing backlash");
            ra_is_clearing_backlash = 1;
            ra_backlash_pulses = get_ra_pulses_raw();
        }
    } else { //to negative
        if (ra_is_clearing_backlash == 1) { 
            // LOGI(TAG, "positive clearing backlash revert to negative");
            ra_is_clearing_backlash = -1;
            ra_backlash_pulses = ra_backlash_pulses + CONFIG_RA_BACKLASH_PULSES; //expected_pulses_after_positive_clearing
        } else if (ra_is_clearing_backlash == -1) { //negative clearing
            
        } else { //not clearing
            // LOGI(TAG, "negative clearing backlash");
            ra_is_clearing_backlash = -1;
            ra_backlash_pulses = get_ra_pulses_raw();
        }
    }
}

void ra_encoder_pul_callback(rencoder_t* target, int32_t pul, int8_t diff, void* args){
    if (ra_is_clearing_backlash == 1) {
        if (pul >= ra_backlash_pulses + CONFIG_RA_BACKLASH_PULSES) {//clearing finished
            int32_t diff_after_clear = pul - ra_backlash_pulses - CONFIG_RA_BACKLASH_PULSES;
            ra_is_clearing_backlash = false;
            ra_actual_pulses += diff_after_clear;
        }
    } else if (ra_is_clearing_backlash == -1) {
        if (pul <= ra_backlash_pulses - CONFIG_RA_BACKLASH_PULSES) {//clearing finished
            int32_t diff_after_clear = pul - ra_backlash_pulses + CONFIG_RA_BACKLASH_PULSES;
            ra_is_clearing_backlash = false;
            ra_actual_pulses += diff_after_clear;
        }
    } else {
        ra_actual_pulses += diff;
    }
}

void dec_encoder_dir_callback(rencoder_t* target, bool dir, void* args){
    if (dir) {//to positive
        if (dec_is_clearing_backlash == 1) { 
            //already clearing positive clearing, do nothing
        } else if (dec_is_clearing_backlash == -1) { //negative clearing
            //covert to positive clearing
            // LOGI(TAG, "negative clearing backlash revert to positive");
            dec_is_clearing_backlash = 1;
            dec_backlash_pulses = dec_backlash_pulses - CONFIG_DEC_BACKLASH_PULSES; //expected_pulses_after_negative_clearing
        } else { //not clearing
            // LOGI(TAG, "positive clearing backlash");
            dec_is_clearing_backlash = 1;
            dec_backlash_pulses = get_dec_pulses_raw();
        }
    } else { //to negative
        if (dec_is_clearing_backlash == 1) { 
            // LOGI(TAG, "positive clearing backlash revert to negative");
            dec_is_clearing_backlash = -1;
            dec_backlash_pulses = dec_backlash_pulses + CONFIG_DEC_BACKLASH_PULSES; //expected_pulses_after_positive_clearing
        } else if (dec_is_clearing_backlash == -1) { //negative clearing
            
        } else { //not clearing
            // LOGI(TAG, "negative clearing backlash");
            dec_is_clearing_backlash = -1;
            dec_backlash_pulses = get_dec_pulses_raw();
        }
    }
}

void dec_encoder_pul_callback(rencoder_t* target, int32_t pul, int8_t diff, void* args){
    if (dec_is_clearing_backlash == 1) {
        if (pul >= dec_backlash_pulses + CONFIG_DEC_BACKLASH_PULSES) {//clearing finished
            int32_t diff_after_clear = pul - dec_backlash_pulses - CONFIG_DEC_BACKLASH_PULSES;
            dec_is_clearing_backlash = false;
            dec_actual_pulses += diff_after_clear;
        }
    } else if (dec_is_clearing_backlash == -1) {
        if (pul <= dec_backlash_pulses - CONFIG_DEC_BACKLASH_PULSES) {//clearing finished
            int32_t diff_after_clear = pul - dec_backlash_pulses + CONFIG_DEC_BACKLASH_PULSES;
            dec_is_clearing_backlash = false;
            dec_actual_pulses += diff_after_clear;
        }
    } else {
        dec_actual_pulses += diff;
    }
}

double ra_pulse_ratio = ((double) SIDEREAL_DAY_MILLIS) / (CONFIG_GPIO_RA_RENCODER_PULSES * CONFIG_RA_GEAR_RATIO);
double dec_pulse_ratio = ((double) DAY_MILLIS) / (CONFIG_GPIO_DEC_RENCODER_PULSES * CONFIG_DEC_GEAR_RATIO);
double ra_time_ratio = ((double) DAY_MILLIS / (double) SIDEREAL_DAY_MILLIS);

int32_t get_ra_angle_millis() {
    double time_offset_millis = currentTimeMillis() - encoder_reset_time;
    double ra_moved_millis = (double)(ra_pulse_ratio * ra_actual_pulses);
    return (int32_t)(ra_time_ratio * (reset_ra_angle_millis + time_offset_millis - ra_moved_millis));
}

int32_t get_dec_angle_millis() {
    int32_t dec_moved_millis = (int32_t)(dec_pulse_ratio * dec_actual_pulses);
    return reset_dec_angle_millis + dec_moved_millis;
}

void set_angles(int32_t ra_angle_day_millis, int32_t dec_angle_day_millis) {
    encoder_reset_time = currentTimeMillis();
    int32_t ra_angle_sidereal_millis = (int32_t)((double)ra_angle_day_millis / ra_time_ratio);
    reset_ra_angle_millis = ra_angle_sidereal_millis;
    reset_dec_angle_millis = dec_angle_day_millis;

    ra_actual_pulses = 0;
    dec_actual_pulses = 0;
}