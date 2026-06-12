#include "arp.h"
#include "net.h"
#include "terminal.h"
#include "idt.h"      // timer_get_ticks
#include <stdint.h>

// Estrutura do pacote ARP (IPv4 over Ethernet)
typedef struct {
    uint16_t htype;   // 0x0001 = Ethernet
    uint16_t ptype;   // 0x0800 = IPv4
    uint8_t  hlen;    // 6
    uint8_t  plen;    // 4
    uint16_t oper;    // 1=request, 2=reply
    uint8_t  sha[6];  // sender MAC
    uint8_t  spa[4];  // sender IP
    uint8_t  tha[6];  // target MAC
    uint8_t  tpa[4];  // target IP
} __attribute__((packed)) ArpPkt;

// Tabela ARP
#define ARP_TABLE_SIZE  16

ArpEntry arp_table[ARP_TABLE_SIZE];

static const uint8_t broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

// ---- Helpers ----

static int ip_eq(const uint8_t a[4], const uint8_t b[4]) {
    return a[0]==b[0] && a[1]==b[1] && a[2]==b[2] && a[3]==b[3];
}

static void arp_table_add(const uint8_t ip[4], const uint8_t mac[6]) {
    // Atualiza se já existe
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (arp_table[i].valid && ip_eq(arp_table[i].ip, ip)) {
            for (int j = 0; j < 6; j++) arp_table[i].mac[j] = mac[j];
            return;
        }
    }
    // Insere em slot livre
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (!arp_table[i].valid) {
            for (int j = 0; j < 4; j++) arp_table[i].ip[j]  = ip[j];
            for (int j = 0; j < 6; j++) arp_table[i].mac[j] = mac[j];
            arp_table[i].valid = 1;
            return;
        }
    }
}

// ---- Envio de ARP request ----
static void arp_send_request(const uint8_t target_ip[4]) {
    ArpPkt pkt;
    pkt.htype = htons(0x0001);
    pkt.ptype = htons(0x0800);
    pkt.hlen  = 6;
    pkt.plen  = 4;
    pkt.oper  = htons(1);
    for (int i = 0; i < 6; i++) { pkt.sha[i] = net_mac[i]; pkt.tha[i] = 0; }
    for (int i = 0; i < 4; i++) { pkt.spa[i] = net_ip[i];  pkt.tpa[i] = target_ip[i]; }
    net_send_eth(broadcast, ETH_ARP, (const uint8_t *)&pkt, sizeof(pkt));
}

// ---- Recepção ----
void arp_handle(const uint8_t *data, uint16_t len) {
    if (len < sizeof(ArpPkt)) return;
    const ArpPkt *p = (const ArpPkt *)data;
    if (htons(p->htype) != 1)      return;
    if (htons(p->ptype) != 0x0800) return;

    // Aprende o sender sempre
    arp_table_add(p->spa, p->sha);

    // Se for request para o nosso IP, responde
    if (htons(p->oper) == 1 && ip_eq(p->tpa, net_ip)) {
        ArpPkt reply;
        reply.htype = htons(0x0001);
        reply.ptype = htons(0x0800);
        reply.hlen  = 6;
        reply.plen  = 4;
        reply.oper  = htons(2);
        for (int i = 0; i < 6; i++) { reply.sha[i] = net_mac[i]; reply.tha[i] = p->sha[i]; }
        for (int i = 0; i < 4; i++) { reply.spa[i] = net_ip[i];  reply.tpa[i] = p->spa[i]; }
        net_send_eth(p->sha, ETH_ARP, (const uint8_t *)&reply, sizeof(reply));
    }
}

// ---- API pública ----

int arp_lookup(const uint8_t ip[4], uint8_t mac_out[6]) {
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (arp_table[i].valid && ip_eq(arp_table[i].ip, ip)) {
            for (int j = 0; j < 6; j++) mac_out[j] = arp_table[i].mac[j];
            return 1;
        }
    }
    return 0;
}

int arp_resolve(const uint8_t ip[4], uint8_t mac_out[6], uint32_t timeout_ms) {
    if (arp_lookup(ip, mac_out)) return 1;
    arp_send_request(ip);
    uint32_t start = timer_get_ticks();
    while (timer_get_ticks() - start < timeout_ms) {
        net_poll();
        if (arp_lookup(ip, mac_out)) return 1;
    }
    return 0;
}

void arp_print_table(void) {
    int found = 0;
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (!arp_table[i].valid) continue;
        found = 1;
        uint8_t *ip  = arp_table[i].ip;
        uint8_t *mac = arp_table[i].mac;
        // IP
        for (int j = 0; j < 4; j++) {
            uint8_t b = ip[j];
            if (b >= 100) terminal_putchar('0' + b/100);
            if (b >= 10)  terminal_putchar('0' + (b/10)%10);
            terminal_putchar('0' + b%10);
            if (j < 3) terminal_putchar('.');
        }
        terminal_writestring("  →  ");
        // MAC
        const char *h = "0123456789ABCDEF";
        for (int j = 0; j < 6; j++) {
            terminal_putchar(h[mac[j] >> 4]);
            terminal_putchar(h[mac[j] & 0xF]);
            if (j < 5) terminal_putchar(':');
        }
        terminal_putchar('\n');
    }
    if (!found) terminal_writestring("Tabela ARP vazia.\n");
}
