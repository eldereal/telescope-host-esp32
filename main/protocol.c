#include "protocol.h"
#include "lwip/sockets.h"

#define IP(B) (*((uint32_t*)(B)))
#define PORT(B) (*((uint16_t*)((B) + 4)))
#define RA(B) (*((uint32_t*)((B) + 6)))
#define DEC(B) (*((uint32_t*)((B) + 10)))

void set_broadcast_fields(
    broadcast_t *target,
    uint32_t ip,
    uint16_t port,
    int32_t ra, //in millis
    int32_t dec //in millis    
) {
    target -> size = 14;
    IP(target->buffer) = htonl(ip);
    PORT(target->buffer) = htons(port);
    RA(target->buffer) = htonl(ra);
    DEC(target->buffer) = htonl(dec);
}