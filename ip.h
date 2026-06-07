#ifndef IP_H
#define IP_H
#include <stdint.h>

#define IP_PROTO_ICMP  1
#define IP_PROTO_TCP   6
#define IP_PROTO_UDP   17
#define IP_PROTO_TCP   6

typedef struct {
    uint8_t  version_ihl;
    uint8_t  dscp_ecn;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint8_t  src[4];
    uint8_t  dst[4];
} __attribute__((packed)) IpHeader;

int  ip_send(const uint8_t dst[4], uint8_t proto,
             const uint8_t *payload, uint16_t plen);
void ip_handle(const uint8_t *data, uint16_t len);
#endif
