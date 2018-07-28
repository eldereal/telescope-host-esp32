#include "esp_timer.h"
#include "slew.h"
#include "mount_encoder.h"
#include "math.h"
#include "util.h"
#include "astro.h"

#define TAG "SLEW"

slew_set_motor_speed_callback motor_callback;
bool slewing = false;
int32_t raStartMillis = 0, decStartMillis = 0;
int32_t raTargetMillis = 0, decTargetMillis = 0;
int speed;
int checkIntervalMillis;
double distance;
double progress;
uint32_t timeToGoMillis;
esp_timer_handle_t slewTimer;

#define MAX_SPEED 16
#define TOLERANCE_MILLIS 1000 
#define CHECK_INTERVAL_MILLIS 1000
#define MIN_CHECK_INTERVAL_MILLIS 125
#define MIN_SPEED 1


double dist(double a, double b) {
    return sqrt(a*a + b*b);
}

double get_slew_progress() {
    return 1 - progress;
}

uint32_t get_slew_time_to_go_millis(){
    return timeToGoMillis;
}

int32_t getRaDiff(int32_t target, int32_t current) {
    int32_t targetGreater, targetLess;
    
    targetGreater = target;
    while(targetGreater > current) targetGreater -= DAY_MILLIS;
    while(targetGreater < current) targetGreater += DAY_MILLIS;

    targetLess = targetGreater - DAY_MILLIS;
    
    int32_t diffGreater = current - targetGreater;
    int32_t diffLess = current - targetLess;

    return abs(diffGreater) < abs(diffLess) ? diffGreater : diffLess;
}

void slew_timer_callback(void* _) {
    int32_t raDiff = getRaDiff(raTargetMillis, get_ra_angle_millis());
    int32_t decDiff = decTargetMillis - get_dec_angle_millis();
    int32_t absRaDiff;
    int32_t raReverse;
    int32_t absDecDiff;
    int32_t decReverse;
    if (raDiff > 0) {
        absRaDiff = raDiff;
        raReverse = 1;
    } else {
        absRaDiff = -raDiff;
        raReverse = -1;
    }
    if (decDiff > 0) {
        absDecDiff = decDiff;
        decReverse = 1;
    } else {
        absDecDiff = -decDiff;
        decReverse = -1;
    }
    if (absRaDiff < TOLERANCE_MILLIS && absDecDiff < TOLERANCE_MILLIS) {
        slewing = false;
        motor_callback(0, 0);
        return;
    }
    double raSpeedFactor, decSpeedFactor;
    if (absRaDiff < absDecDiff) {
        raSpeedFactor = (double)absRaDiff / (double)absDecDiff;
        decSpeedFactor = 1;
        timeToGoMillis = absDecDiff / speed;
    } else {
        raSpeedFactor = 1;
        decSpeedFactor = (double)absDecDiff / (double)absRaDiff;
        timeToGoMillis = absRaDiff / speed;
    }
    while (timeToGoMillis < checkIntervalMillis * 4) {
        LOGI(TAG, "Near target, slow down");
        bool anySlowDown = false;
        if (checkIntervalMillis > MIN_CHECK_INTERVAL_MILLIS) {
            checkIntervalMillis /= 2;
            anySlowDown = true;
        }
        if (speed > MIN_SPEED) {
            speed /= 2;
            timeToGoMillis *= 2;
            anySlowDown = true;
        }
        if (!anySlowDown) {
            break;
        }
    }
    motor_callback(speed * raSpeedFactor * raReverse, speed * decSpeedFactor * decReverse);
    double distanceNow = dist(raDiff, decDiff);
    progress = distanceNow / distance;
    LOGI(TAG, "distance: %.2f/%.2f raDiff: %d, decDiff: %d, time: %d", distanceNow, distance, raDiff, decDiff, timeToGoMillis / 1000);
    esp_timer_start_once(slewTimer, checkIntervalMillis * 1000);
}

esp_err_t init_slew(slew_set_motor_speed_callback callback) {
    motor_callback = callback;
    esp_timer_create_args_t args = {
        .dispatch_method = ESP_TIMER_TASK,
        .callback = slew_timer_callback
    };
    return esp_timer_create(&args, &slewTimer);
}

bool is_slewing(){
    return slewing;
}

void abort_slew() {
    motor_callback(0, 0);
    esp_timer_stop(slewTimer);
    slewing = false;
}

void slew_to_coordinates(int32_t raMillis, int32_t decMillis){
    raStartMillis = get_ra_angle_millis();
    decStartMillis = get_dec_angle_millis();
    raTargetMillis = raMillis;
    decTargetMillis = decMillis;
    distance = dist(getRaDiff(raTargetMillis, raStartMillis), decTargetMillis - decStartMillis);
    slewing = true;
    speed = MAX_SPEED;
    checkIntervalMillis = CHECK_INTERVAL_MILLIS;
    slew_timer_callback(NULL);
}

