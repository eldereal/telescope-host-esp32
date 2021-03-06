#include "protocol.h"
#include "lwip/sockets.h"

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
) {
    BROADCAST_IP(target->buffer) = htonl(ip);
    BROADCAST_PORT(target->buffer) = htons(port);
    BROADCAST_RA(target->buffer) = htonl(ra);
    BROADCAST_DEC(target->buffer) = htonl(dec);
    BROADCAST_SLEWING(target->buffer) = slewing ? 1 : 0;
    BROADCAST_TRACKING(target->buffer) = tracking ? 1 : 0;
    BROADCAST_RA_SPEED(target->buffer) = htonl(ra_speed);
    BROADCAST_DEC_SPEED(target->buffer) = htonl(dec_speed);
    BROADCAST_SIDE_OF_PIER(target->buffer) = side_of_pier;
    BROADCAST_FOCUSER_MAX_STEPS(target->buffer) = htonl(focuser_max_steps);
    BROADCAST_FOCUSER_NANOS_PER_STEP(target->buffer) = htons(focuser_nanos_per_step);
    BROADCAST_FOCUSER_RUNNING(target->buffer) = focuser_running;
}

void set_ack_fields(
    ack_t *target,
    uint32_t cmd_id
) {
    ACK_CMD_ID(target->buffer) = htonl(cmd_id);
    ACK_ZERO(target->buffer) = 0;
}