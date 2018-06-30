#include "esp_timer.h"

uint64_t currentTimeMillis(){
    return esp_timer_get_time() / 1000;
}