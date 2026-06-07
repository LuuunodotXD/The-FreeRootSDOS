#include "udp.h"
#include "ip.h"
#include "net.h"
#include <stdint.h>

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed)) UdpHeader;

#define UDP_MAX_LISTENERS  8

typedef struct {
    uint16_t  port;
    udp_rx_cb cb;
} UdpListener;

static UdpListener listeners[UDP_MAX_LISTENERS];

void udp_listen(uint16_t port, udp_rx_cb cb) {
    // Atualiza se porta já registrada
    for (int i = 0; i < UDP_MAX_LISTENERS; i++) {
        if (listeners[i].port == port) {
            listeners[i].cb = cb;
            return;
        }
    }
    // Insere em slot livre
    for (int i = 0; i < UDP_MAX_LISTENERS; i++) {
        if (!listeners[i].port) {
            listeners[i].port = port;
            listeners[i].cb   = cb;
            return;
        }
    }
}

int udp_send(const uint8_t dst_ip[4], uint16_t dst_port, uint16_t src_port,
             const uint8_t *data, uint16_t len) {
    static uint8_t buf[8 + 1472];
    if (len > 1472) return -1;

    UdpHeader *hdr = (UdpHeader *)buf;
    hdr->src_port = htons(src_port);
    hdr->dst_port = htons(dst_port);
    hdr->length   = htons((uint16_t)(8 + len));
    hdr->checksum = 0;  // opcional em IPv4

    for (int i = 0; i < len; i++) buf[8 + i] = data[i];

    return ip_send(dst_ip, IP_PROTO_UDP, buf, (uint16_t)(8 + len));
}

void udp_handle(const uint8_t *src_ip, const uint8_t *data, uint16_t len) {
    if (len < 8) return;
    const UdpHeader *hdr = (const UdpHeader *)data;
    uint16_t dst_port  = htons(hdr->dst_port);
    uint16_t src_port  = htons(hdr->src_port);
    uint16_t udp_len   = htons(hdr->length);
    if (udp_len < 8 || udp_len > len) return;
    const uint8_t *payload = data + 8;
    uint16_t plen = udp_len - 8;

    for (int i = 0; i < UDP_MAX_LISTENERS; i++) {
        if (listeners[i].port == dst_port && listeners[i].cb)
            listeners[i].cb(src_ip, src_port, payload, plen);
    }
}
