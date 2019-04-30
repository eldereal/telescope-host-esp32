#include "mount.h"
#include "driver/ledc.h"
#include "util.h"
#include "astro.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "math.h"
#include "mount_encoder.h"

/* ------ utils ----------- */
#define DUTY_RES LEDC_TIMER_13_BIT
#define DUTY (((1 << DUTY_RES) - 1) / 2)

#define GPIO_RA_EN  (CONFIG_GPIO_RA_EN)
#define GPIO_RA_DIR (CONFIG_GPIO_RA_DIR)
#define GPIO_RA_PUL (CONFIG_GPIO_RA_PUL)

#define RA_GEAR_RATIO ((double)(CONFIG_RA_GEAR_RATIO))
#define RA_RESOLUTION ((double)(CONFIG_RA_RESOLUTION))
#define RA_CYCLE_STEPS ((double)(CONFIG_RA_CYCLE_STEPS))

#define GPIO_DEC_EN  (CONFIG_GPIO_DEC_EN)
#define GPIO_DEC_DIR (CONFIG_GPIO_DEC_DIR)
#define GPIO_DEC_PUL (CONFIG_GPIO_DEC_PUL)

#define DEC_GEAR_RATIO ((double)(CONFIG_DEC_GEAR_RATIO))
#define DEC_RESOLUTION ((double)(CONFIG_DEC_RESOLUTION))
#define DEC_CYCLE_STEPS ((double)(CONFIG_DEC_CYCLE_STEPS))

#ifndef CONFIG_RA_REVERSE
#define CONFIG_RA_REVERSE false
#endif

#ifndef CONFIG_DEC_REVERSE
#define CONFIG_DEC_REVERSE false
#endif

/* ---------- FREQS ---------- */
#define RA_FREQ(cyclesPerSiderealDay) (RA_CYCLE_STEPS * RA_GEAR_RATIO * RA_RESOLUTION * (cyclesPerSiderealDay) * 1000 / SIDEREAL_DAY_MILLIS)
#define DEC_FREQ(cyclesPerDay) (DEC_CYCLE_STEPS * DEC_GEAR_RATIO * DEC_RESOLUTION * (cyclesPerDay) * 1000 / DAY_MILLIS)

const static char *TAG = "Mount";

double raCyclesPerSiderealDay;
double decCyclesPerDay;
double timeRatio;

ledc_channel_config_t ra_pmw_channel = {
    .channel = LEDC_CHANNEL_0,
    .timer_sel = LEDC_TIMER_0,
    .duty = 0,
    .gpio_num = GPIO_RA_PUL,
    .speed_mode = LEDC_HIGH_SPEED_MODE,
};

ledc_timer_config_t ra_pmw_timer = {
    .freq_hz = RA_FREQ(1),
    .duty_resolution = DUTY_RES,
    .speed_mode = LEDC_HIGH_SPEED_MODE,
    .timer_num = LEDC_TIMER_0
};

ledc_channel_config_t dec_pmw_channel = {
    .channel = LEDC_CHANNEL_1,
    .timer_sel = LEDC_TIMER_1,
    .duty = 0,
    .gpio_num = GPIO_DEC_PUL,
    .speed_mode = LEDC_HIGH_SPEED_MODE,
};

ledc_timer_config_t dec_pmw_timer = {
    .freq_hz = DEC_FREQ(1),
    .duty_resolution = DUTY_RES,
    .speed_mode = LEDC_HIGH_SPEED_MODE,
    .timer_num = LEDC_TIMER_1
};

esp_err_t init_mount() {
    raCyclesPerSiderealDay = 0;
    decCyclesPerDay = 0;
    
    nvs_handle my_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);

    if (err == ESP_OK) {
        int32_t timeRatioMillion = 0;
        err = nvs_get_i32(my_handle, "time_ratio", &timeRatioMillion);
        switch (err) {
            case ESP_OK:
                timeRatio = timeRatioMillion / 1000000.0; 
                LOGI(TAG, "Read time ratio: %f", timeRatio);
                break;
            case ESP_ERR_NVS_NOT_FOUND:
                timeRatio = 1;
                LOGI(TAG, "Time ratio not set");                
                break;
            default :
                timeRatio = 1;
                LOGI(TAG, "Time ratio read error: %d", err);
        }
        nvs_close(my_handle);
    } else {
        LOGI(TAG, "Time ratio storage error: %d", err);
        timeRatio = 1;
    }
    
    gpio_pad_select_gpio(GPIO_RA_DIR);
    gpio_set_direction(GPIO_RA_DIR, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_RA_DIR, 1);

    gpio_pad_select_gpio(GPIO_RA_EN);
    gpio_set_direction(GPIO_RA_EN, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_RA_EN, 1);

    gpio_pad_select_gpio(GPIO_DEC_DIR);
    gpio_set_direction(GPIO_DEC_DIR, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_DEC_DIR, 1);

    gpio_pad_select_gpio(GPIO_DEC_EN);
    gpio_set_direction(GPIO_DEC_EN, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_DEC_EN, 1);

    ledc_channel_config(&ra_pmw_channel);
    ledc_timer_config(&ra_pmw_timer);

    ledc_channel_config(&dec_pmw_channel);
    ledc_timer_config(&dec_pmw_timer);
    return ESP_OK;
}

void set_mount_time_ratio_persist(double ratio) {
    timeRatio = ratio;
    set_ra_cycles_per_sidereal_day(raCyclesPerSiderealDay);
    set_dec_cycles_per_day(decCyclesPerDay);

    nvs_handle my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);

    if (err == ESP_OK) {
        int32_t timeRatioMillion = (int32_t)(timeRatio * 1000000.0);
        err = nvs_set_i32(my_handle, "time_ratio", timeRatioMillion);
        if (err == ESP_OK) {
            LOGI(TAG, "Set time ratio success");
        } else {
            LOGE(TAG, "Set time ratio failed: %d", err);
        }
        nvs_close(my_handle);
    } else {
        LOGE(TAG, "Time ratio storage error: %d", err);
    }
}

double get_mount_time_ratio() {
    return timeRatio;
}

void set_ra_cycles_per_sidereal_day(double value) {
    raCyclesPerSiderealDay = value;
    bool freqIsNeg = false;
    if (raCyclesPerSiderealDay < 0) {
        raCyclesPerSiderealDay = -raCyclesPerSiderealDay;
        freqIsNeg = true;
        if (CONFIG_RA_REVERSE) {
            gpio_set_level(GPIO_RA_DIR, 1);
        } else {
            gpio_set_level(GPIO_RA_DIR, 0);
        }        
    } else {
        if (CONFIG_RA_REVERSE) {
            gpio_set_level(GPIO_RA_DIR, 0);
        } else {
            gpio_set_level(GPIO_RA_DIR, 1);
        }
    }

    double raActualFreq = timeRatio * RA_FREQ(raCyclesPerSiderealDay);
    int rafreq = (int) round(raActualFreq);
    if (raCyclesPerSiderealDay < RA_CYCLE_MIN || rafreq == 0) {
        LOGI(TAG, "RA Stop");
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, ra_pmw_channel.channel, 0);
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, ra_pmw_channel.channel);
        gpio_set_level(GPIO_RA_EN, 1);
        ra_pulse_freq_changed(0);
    } else {
        if (raCyclesPerSiderealDay > RA_CYCLE_MAX) raCyclesPerSiderealDay = RA_CYCLE_MAX;        
        LOGI(TAG, "RA Freq: %d (%f)", rafreq, raActualFreq);
        ledc_set_freq(LEDC_HIGH_SPEED_MODE, ra_pmw_timer.timer_num, rafreq);
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, ra_pmw_channel.channel, DUTY);        
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, ra_pmw_channel.channel);
        gpio_set_level(GPIO_RA_EN, 0);
        ra_pulse_freq_changed(freqIsNeg ? -rafreq : rafreq);
    }
}

void set_dec_cycles_per_day(double value) {
    decCyclesPerDay = value;
    bool freqIsNeg = false;
    if (decCyclesPerDay < 0) {
        decCyclesPerDay = -decCyclesPerDay;
        freqIsNeg = true;
        if (CONFIG_DEC_REVERSE) {
            gpio_set_level(GPIO_DEC_DIR, 1);
        } else {
            gpio_set_level(GPIO_DEC_DIR, 0);
        } 
    } else {
        if (CONFIG_DEC_REVERSE) {
            gpio_set_level(GPIO_DEC_DIR, 0);
        } else {
            gpio_set_level(GPIO_DEC_DIR, 1);
        } 
    }

    double decActualFreq = timeRatio * DEC_FREQ(decCyclesPerDay);
    int decfreq = (int) round(decActualFreq);
    if (decCyclesPerDay < DEC_CYCLE_MIN || decfreq == 0) {
        LOGI(TAG, "DEC Stop");
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, dec_pmw_channel.channel, 0);
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, dec_pmw_channel.channel);
        gpio_set_level(GPIO_DEC_EN, 1);
        dec_pulse_freq_changed(0);
    } else {
        if (decCyclesPerDay > DEC_CYCLE_MAX) decCyclesPerDay = DEC_CYCLE_MAX;        
        LOGI(TAG, "DEC Freq: %d (%f)", decfreq, decActualFreq);
        ledc_set_freq(LEDC_HIGH_SPEED_MODE, dec_pmw_timer.timer_num, decfreq);
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, dec_pmw_channel.channel, DUTY);        
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, dec_pmw_channel.channel);
        gpio_set_level(GPIO_DEC_EN, 0);
        dec_pulse_freq_changed(freqIsNeg ? -decfreq : decfreq);
    }
}

double get_ra_cycles_per_sidereal_day() {
    return raCyclesPerSiderealDay;
}

double get_dec_cycles_per_day(){
    return decCyclesPerDay;
}