#ifndef FOCUSER_H
#define FOCUSER_H
#include "stdint.h"
#include "stdbool.h"

void focuser_init();
uint16_t focuser_get_movement_nanos_per_step();
uint32_t focuser_get_max_steps();
void focuser_move(int32_t steps);
bool focuser_get_is_moving();
void focuser_abort_move();

#endif