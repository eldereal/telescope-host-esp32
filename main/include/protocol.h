#ifndef __PROTOCOL_H
#define __PROTOCOL_H

#include "freertos/FreeRTOS.h"

typedef struct broadcast {
    uint32_t size;
    uint8_t buffer[16];
} broadcast_t;

void set_broadcast_fields(
    broadcast_t *target,
    uint32_t ip,
    uint16_t port,
    int32_t ra, //in millis
    int32_t dec //in millis    
);
    // /* IP     */ *(uint32_t*)(buffer    )  = htonl(my_ip_num);
    // /* Port   */ *(uint16_t*)(buffer + 4)  = htons(UDP_PORT);
    // /* RADIR  */ *(uint8_t* )(buffer + 6)  = get_ra_direction() ? 1 : 0;
    // /* RATICK */ *(int32_t* )(buffer + 7)  = htonl(get_ra_pulses_raw());
    // /* DECDIR */ *(uint8_t* )(buffer + 11) = get_dec_direction() ? 1 : 0;
    // /* DECTICK*/ *(int32_t* )(buffer + 12) = htonl(get_dec_pulses_raw());


#endif