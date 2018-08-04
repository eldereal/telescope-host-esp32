#include "freertos/FreeRTOS.h"

uint8_t getSideOfPier();
int32_t decMillis2decMecMillis(int32_t decMillis);
int32_t decMecMillis2decMillis(int32_t decMecMillis, uint8_t* parseSideOfPier);
void setSideOfPierWithDecMecMillis(int32_t decMecMillis);