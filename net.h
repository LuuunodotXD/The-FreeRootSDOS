#ifndef NET_H
#define NET_H
#include <stdint.h>

// Big-endian helpers
static inline uint16_t htons(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
static inline uint32_t htonl(uint32_t v) {
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8)
         | ((v >> 8) & 0xFF00) | ((v >> 24) & 0xFF);
}
static inline uint16_t checksum16(const uint8_t *data, int len) {
    uint32_t sum = 0;
    for (int i = 0; i + 1 < len; i += 2)
        sum += ((uint32_t)data[i] << 8) | data[i+1];
    if (len & 1) sum += (uint32_t)data[len-1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}
#define ntohs htons
#define ntohl htonl

// Cabeçalho Ethernet
typedef struct {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t ethertype;   // big-endian
} __attribute__((packed)) EthHeader;

#define ETH_ARP   0x0806
#define ETH_IP    0x0800

// Configuração IP do sistema (QEMU user-mode defaults)
extern uint8_t  net_mac[6];
extern uint8_t  net_ip[4];
extern uint8_t  net_gw[4];
extern uint8_t  net_mask[4];

void net_init(void);
void net_poll(void);
int  net_send_eth(const uint8_t dst[6], uint16_t ethertype,
                  const uint8_t *payload, uint16_t len);

// Chamado pelo dispatcher quando chega um frame
void arp_handle(const uint8_t *data, uint16_t len);  // registrado internamente

#endif
