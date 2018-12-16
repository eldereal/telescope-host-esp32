#ifndef __PROTOCOL_H
#define __PROTOCOL_H

#include "freertos/FreeRTOS.h"

#define BROADCAST_IP(B) (*((uint32_t*)(B)))
#define BROADCAST_PORT(B) (*((uint16_t*)((B) + 4)))
#define BROADCAST_RA(B) (*((int32_t*)((B) + 6)))
#define BROADCAST_DEC(B) (*((int32_t*)((B) + 10)))
#define BROADCAST_SLEWING(B) (*((uint8_t*)((B) + 14)))
#define BROADCAST_TRACKING(B) (*((uint8_t*)((B) + 15)))
#define BROADCAST_RA_SPEED(B) (*((int32_t*)((B) + 16)))
#define BROADCAST_DEC_SPEED(B) (*((int32_t*)((B) + 20)))
#define BROADCAST_SIDE_OF_PIER(B) (*((uint8_t*)((B) + 24)))
#define BROADCAST_FOCUSER_MAX_STEPS(B) (*((uint32_t*)((B) + 25)))
#define BROADCAST_FOCUSER_NANOS_PER_STEP(B) (*((uint16_t*)((B) + 29)))
#define BROADCAST_FOCUSER_RUNNING(B) (*((uint8_t*)((B) + 31)))
#define BROADCAST_SIZE 32

typedef struct broadcast {
    uint8_t buffer[BROADCAST_SIZE];
} broadcast_t;

void set_broadcast_fields(
    broadcast_t *target,
    uint32_t ip,
    uint16_t port,
    int32_t ra, //in millis
    int32_t dec, //in millis
    bool slewing,
    bool tracking,
    int32_t ra_speed, // in milli seconds per sidereal second
    int32_t dec_speed, // in milli seconds per second 
    uint8_t side_of_pier, // 0: Normal (East); 1: Beyond the pole (West)
    uint32_t focuser_max_steps,
    uint16_t focuser_nanos_per_step,
    bool focuser_running
);


    // /* IP     */ *(uint32_t*)(buffer    )  = htonl(my_ip_num);
    // /* Port   */ *(uint16_t*)(buffer + 4)  = htons(UDP_PORT);
    // /* RADIR  */ *(uint8_t* )(buffer + 6)  = get_ra_direction() ? 1 : 0;
    // /* RATICK */ *(int32_t* )(buffer + 7)  = htonl(get_ra_pulses_raw());
    // /* DECDIR */ *(uint8_t* )(buffer + 11) = get_dec_direction() ? 1 : 0;
    // /* DECTICK*/ *(int32_t* )(buffer + 12) = htonl(get_dec_pulses_raw());


#endif