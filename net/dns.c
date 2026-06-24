#include "dns.h"
#include "udp.h"
#include "net.h"
#include "idt.h"
#include <stdint.h>

#define DNS_PORT      53
#define DNS_SRC_PORT  1053

static const uint8_t dns_server[4] = {10, 0, 2, 3};  // QEMU user-mode DNS

// ---- Cache ----
#define DNS_CACHE_SIZE  8
typedef struct { char name[64]; uint8_t ip[4]; int valid; } DnsEntry;
static DnsEntry dns_cache[DNS_CACHE_SIZE];

static int str_eq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && a[i] == b[i]) i++;
    return !a[i] && !b[i];
}

static int cache_lookup(const char *name, uint8_t ip[4]) {
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (!dns_cache[i].valid) continue;
        if (str_eq(dns_cache[i].name, name)) {
            for (int j = 0; j < 4; j++) ip[j] = dns_cache[i].ip[j];
            return 1;
        }
    }
    return 0;
}

static void cache_store(const char *name, const uint8_t ip[4]) {
    int slot = 0;
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (!dns_cache[i].valid) { slot = i; break; }
    }
    int j = 0;
    while (name[j] && j < 63) { dns_cache[slot].name[j] = name[j]; j++; }
    dns_cache[slot].name[j] = 0;
    for (int k = 0; k < 4; k++) dns_cache[slot].ip[k] = ip[k];
    dns_cache[slot].valid = 1;
}

// ---- Codifica "www.google.com" → \x03www\x06google\x03com\x00 ----
static int encode_name(const char *name, uint8_t *out, int maxlen) {
    int pos = 0;
    while (*name) {
        int len = 0;
        const char *p = name;
        while (*p && *p != '.') { p++; len++; }
        if (pos + 1 + len >= maxlen) return -1;
        out[pos++] = (uint8_t)len;
        for (int i = 0; i < len; i++) out[pos++] = (uint8_t)name[i];
        name += len;
        if (*name == '.') name++;
    }
    if (pos >= maxlen) return -1;
    out[pos++] = 0;
    return pos;
}

// ---- Avança além de um nome DNS (lida com compressão) ----
static int skip_name(const uint8_t *msg, int mlen, int off) {
    int first = -1;
    while (off < mlen) {
        uint8_t b = msg[off];
        if ((b & 0xC0) == 0xC0) {
            if (first < 0) first = off + 2;
            if (off + 1 >= mlen) return -1;
            off = ((b & 0x3F) << 8) | msg[off + 1];
        } else if (b == 0) {
            if (first < 0) first = off + 1;
            return first;
        } else {
            off += 1 + b;
        }
    }
    return -1;
}

// ---- Estado da resposta ----
static volatile int     got_reply = 0;
static volatile uint8_t reply_ip[4];
static uint16_t         query_id  = 0xAB00;

// ---- Callback UDP ----
static void dns_rx(const uint8_t *src_ip, uint16_t src_port,
                   const uint8_t *data, uint16_t len) {
    (void)src_ip; (void)src_port;
    if (len < 12) return;

    uint16_t id      = (uint16_t)((data[0] << 8) | data[1]);
    uint16_t flags   = (uint16_t)((data[2] << 8) | data[3]);
    uint16_t qdcount = (uint16_t)((data[4] << 8) | data[5]);
    uint16_t ancount = (uint16_t)((data[6] << 8) | data[7]);

    if (id != query_id)          return;
    if (!(flags & 0x8000))       return;  // não é resposta
    if ((flags & 0x000F) != 0)   return;  // RCODE != 0
    if (ancount == 0)            return;

    int pos = 12;
    for (int i = 0; i < qdcount && pos < len; i++) {
        pos = skip_name(data, len, pos);
        if (pos < 0) return;
        pos += 4;  // QTYPE + QCLASS
    }

    for (int i = 0; i < ancount && pos < len; i++) {
        pos = skip_name(data, len, pos);
        if (pos < 0 || pos + 10 > len) return;
        uint16_t type  = (uint16_t)((data[pos]   << 8) | data[pos+1]);
        uint16_t rdlen = (uint16_t)((data[pos+8] << 8) | data[pos+9]);
        pos += 10;
        if (pos + rdlen > len) return;
        if (type == 1 && rdlen == 4) {  // registro A
            for (int k = 0; k < 4; k++) reply_ip[k] = data[pos + k];
            got_reply = 1;
            return;
        }
        pos += rdlen;
    }
}

// ---- API pública ----
int dns_resolve(const char *hostname, uint8_t ip_out[4], uint32_t timeout_ms) {
    if (cache_lookup(hostname, ip_out)) return 1;

    static uint8_t buf[512];
    query_id++;

    // Cabeçalho DNS
    buf[0]  = (uint8_t)(query_id >> 8);
    buf[1]  = (uint8_t)(query_id & 0xFF);
    buf[2]  = 0x01; buf[3]  = 0x00;   // flags: RD=1
    buf[4]  = 0x00; buf[5]  = 0x01;   // QDCOUNT=1
    buf[6]  = 0x00; buf[7]  = 0x00;   // ANCOUNT=0
    buf[8]  = 0x00; buf[9]  = 0x00;
    buf[10] = 0x00; buf[11] = 0x00;

    int nlen = encode_name(hostname, buf + 12, (int)(sizeof(buf) - 16));
    if (nlen < 0) return 0;

    int pos = 12 + nlen;
    buf[pos++] = 0x00; buf[pos++] = 0x01;  // QTYPE = A
    buf[pos++] = 0x00; buf[pos++] = 0x01;  // QCLASS = IN

    udp_listen(DNS_SRC_PORT, dns_rx);
    got_reply = 0;
    udp_send(dns_server, DNS_PORT, DNS_SRC_PORT, buf, (uint16_t)pos);

    uint32_t t0 = timer_get_ticks();
    while (timer_get_ticks() - t0 < timeout_ms) {
        net_poll();
        if (got_reply) {
            for (int k = 0; k < 4; k++) ip_out[k] = reply_ip[k];
            cache_store(hostname, ip_out);
            udp_listen(DNS_SRC_PORT, 0);
            return 1;
        }
    }
    udp_listen(DNS_SRC_PORT, 0);
    return 0;
}
