#include "net.h"
#include "arp.h"
#include "netdev.h"
#include "ip.h"
#include <stdint.h>

// Configuração padrão QEMU user-mode networking
uint8_t net_mac[6]  = {0};
uint8_t net_ip[4]   = {10, 0, 2, 15};
uint8_t net_gw[4]   = {10, 0, 2,  2};
uint8_t net_mask[4] = {255, 255, 255, 0};

// Buffer de montagem de frames Ethernet
static uint8_t frame_buf[14 + 1500];

// Dispatcher: chamado pelo RTL8139 para cada frame recebido
static void eth_dispatch(const uint8_t *data, uint16_t len) {
    if (len < 14) return;
    const EthHeader *eth = (const EthHeader *)data;
    uint16_t etype = htons(eth->ethertype);
    const uint8_t *payload = data + 14;
    uint16_t plen = len - 14;
    if (etype == ETH_ARP) arp_handle(payload, plen);
    if (etype == ETH_IP) ip_handle(payload, plen);
}

void net_init(void) {
    netdev_get_mac(net_mac);
    netdev_set_rx_callback(eth_dispatch);
}

void net_poll(void) {
    netdev_poll();
}

int net_send_eth(const uint8_t dst[6], uint16_t ethertype,
                 const uint8_t *payload, uint16_t len) {
    if (len + 14 > sizeof(frame_buf)) return -1;
    EthHeader *eth = (EthHeader *)frame_buf;
    for (int i = 0; i < 6; i++) { eth->dst[i] = dst[i]; eth->src[i] = net_mac[i]; }
    eth->ethertype = htons(ethertype);
    for (int i = 0; i < len; i++) frame_buf[14 + i] = payload[i];
    return netdev_send(frame_buf, 14 + len);
}
