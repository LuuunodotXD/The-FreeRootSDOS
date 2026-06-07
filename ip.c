#include "ip.h"
#include "net.h"
#include "arp.h"
#include "udp.h"
#include "tcp.h"
#include <stdint.h>

// Forward declaration do handler ICMP
void icmp_handle(const uint8_t *ip_src, const uint8_t *data, uint16_t len);

static uint16_t ip_id = 0;

static int same_subnet(const uint8_t dst[4]) {
    for (int i = 0; i < 4; i++)
        if ((dst[i] & net_mask[i]) != (net_ip[i] & net_mask[i])) return 0;
    return 1;
}

int ip_send(const uint8_t dst[4], uint8_t proto,
            const uint8_t *payload, uint16_t plen) {
    static uint8_t buf[20 + 1480];
    if (plen > 1480) return -1;

    // Resolve MAC: destino direto ou gateway
    uint8_t mac[6];
    const uint8_t *target = same_subnet(dst) ? dst : net_gw;
    if (!arp_resolve(target, mac, 2000)) return -2;

    // Cabeçalho IP
    IpHeader *hdr = (IpHeader *)buf;
    hdr->version_ihl = 0x45;
    hdr->dscp_ecn    = 0;
    hdr->total_len   = htons((uint16_t)(20 + plen));
    hdr->id          = htons(ip_id++);
    hdr->flags_frag  = htons(0x4000);  // Don't Fragment
    hdr->ttl         = 64;
    hdr->protocol    = proto;
    hdr->checksum    = 0;
    for (int i = 0; i < 4; i++) { hdr->src[i] = net_ip[i]; hdr->dst[i] = dst[i]; }
    hdr->checksum    = htons(checksum16(buf, 20));

    for (int i = 0; i < plen; i++) buf[20 + i] = payload[i];
    return net_send_eth(mac, ETH_IP, buf, (uint16_t)(20 + plen));
}

void ip_handle(const uint8_t *data, uint16_t len) {
    if (len < 20) return;
    const IpHeader *hdr = (const IpHeader *)data;

    // Versão e tamanho do header
    if ((hdr->version_ihl >> 4) != 4) return;
    uint8_t ihl = (uint8_t)((hdr->version_ihl & 0x0F) * 4);
    if (ihl < 20 || ihl > len) return;

    // Total length coerente
    uint16_t total = htons(hdr->total_len);
    if (total < ihl || total > len) return;   // ← fix principal

    // Verifica destino: nosso IP ou broadcast
    int for_us = 1, is_bcast = 1;
    for (int i = 0; i < 4; i++) {
        if (hdr->dst[i] != net_ip[i])  for_us  = 0;
        if (hdr->dst[i] != 0xFF)       is_bcast = 0;
    }
    if (!for_us && !is_bcast) return;

    const uint8_t *payload = data + ihl;
    uint16_t plen = (uint16_t)(total - ihl);

    if (hdr->protocol == IP_PROTO_ICMP)
        icmp_handle(hdr->src, payload, plen);
    if (hdr->protocol == IP_PROTO_UDP)
        udp_handle(hdr->src, payload, plen);
    if (hdr->protocol == IP_PROTO_TCP)
        tcp_handle(hdr->src, payload, plen);
}
