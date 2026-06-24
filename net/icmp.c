#include "icmp.h"
#include "ip.h"
#include "net.h"
#include "idt.h"
#include <stdint.h>

typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
    uint8_t  data[32];
} __attribute__((packed)) IcmpEcho;

static uint16_t          ping_id      = 0xAB12;
static volatile int      got_reply    = 0;
static volatile uint16_t reply_seq    = 0;

// ---- Resposta pendente (evita re-entrância no ip_send) ----
static int      pending_reply = 0;
static uint8_t  pending_dst[4];
static uint8_t  pending_pkt[40];
static uint16_t pending_len;

void icmp_init(void) { ping_id = 0xAB12; }

void icmp_handle(const uint8_t *ip_src, const uint8_t *data, uint16_t len) {
    if (len < 8) return;
    const IcmpEcho *pkt = (const IcmpEcho *)data;

    if (pkt->type == 8) {
        // Echo request — enfileira resposta, não chama ip_send diretamente
        IcmpEcho *reply = (IcmpEcho *)pending_pkt;
        reply->type     = 0;
        reply->code     = 0;
        reply->checksum = 0;
        reply->id       = pkt->id;
        reply->seq      = pkt->seq;
        uint16_t dlen   = (uint16_t)(len - 8);
        if (dlen > 32) dlen = 32;
        for (int i = 0; i < dlen; i++) reply->data[i] = pkt->data[i];
        pending_len     = (uint16_t)(8 + dlen);
        reply->checksum = htons(checksum16(pending_pkt, pending_len));
        for (int i = 0; i < 4; i++) pending_dst[i] = ip_src[i];
        pending_reply   = 1;
    }
    else if (pkt->type == 0 && htons(pkt->id) == ping_id) {
        // Echo reply para o nosso ping
        got_reply = 1;
        reply_seq = htons(pkt->seq);
    }
}

// Chamada após net_poll para enviar respostas pendentes
void icmp_flush(void) {
    if (pending_reply) {
        pending_reply = 0;
        ip_send(pending_dst, IP_PROTO_ICMP, pending_pkt, pending_len);
    }
}

int icmp_ping(const uint8_t ip[4], uint16_t seq, uint32_t timeout_ms) {
    IcmpEcho pkt;
    pkt.type     = 8;
    pkt.code     = 0;
    pkt.checksum = 0;
    pkt.id       = htons(ping_id);
    pkt.seq      = htons(seq);
    for (int i = 0; i < 32; i++) pkt.data[i] = (uint8_t)i;
    pkt.checksum = htons(checksum16((uint8_t *)&pkt, 40));

    got_reply = 0;
    uint32_t t0 = timer_get_ticks();
    if (ip_send(ip, IP_PROTO_ICMP, (uint8_t *)&pkt, 40) < 0) return -1;

    while (timer_get_ticks() - t0 < timeout_ms) {
        net_poll();
        icmp_flush();
        if (got_reply && reply_seq == seq)
            return (int)(timer_get_ticks() - t0);
    }
    return -1;
}
