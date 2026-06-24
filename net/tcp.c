#include "tcp.h"
#include "ip.h"
#include "net.h"
#include "idt.h"
#include <stdint.h>

// ---- Flags TCP ----
#define TF_FIN  0x01
#define TF_SYN  0x02
#define TF_RST  0x04
#define TF_PSH  0x08
#define TF_ACK  0x10

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack_seq;
    uint8_t  data_off;    // bits 7:4 = header length em words de 32 bits
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed)) TcpHdr;

// Conexão ativa (uma de cada vez)
static TcpConn *g_conn = 0;

// Porta local dinâmica
static uint16_t next_port = 49152;

// ---- Checksum TCP (com pseudo-header) ----
static uint16_t tcp_cksum(const uint8_t src[4], const uint8_t dst[4],
                           const uint8_t *seg, uint16_t seglen) {
    uint32_t sum = 0;
    sum += ((uint32_t)src[0] << 8) | src[1];
    sum += ((uint32_t)src[2] << 8) | src[3];
    sum += ((uint32_t)dst[0] << 8) | dst[1];
    sum += ((uint32_t)dst[2] << 8) | dst[3];
    sum += 0x0006;          // protocolo TCP
    sum += seglen;
    for (int i = 0; i + 1 < seglen; i += 2)
        sum += ((uint32_t)seg[i] << 8) | seg[i+1];
    if (seglen & 1)
        sum += (uint32_t)seg[seglen-1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

// ---- Envia segmento TCP ----
static int tcp_send_seg(TcpConn *conn, uint8_t flags,
                         const uint8_t *data, uint16_t dlen) {
    static uint8_t buf[20 + 1460];
    uint16_t seglen = (uint16_t)(20 + dlen);
    if (dlen > 1460) return -1;

    TcpHdr *hdr = (TcpHdr *)buf;
    hdr->src_port = htons(conn->local_port);
    hdr->dst_port = htons(conn->remote_port);
    hdr->seq      = htonl(conn->snd_seq);
    hdr->ack_seq  = (flags & TF_ACK) ? htonl(conn->rcv_ack) : 0;
    hdr->data_off = 0x50;           // 5 words de 32 bits = 20 bytes
    hdr->flags    = flags;
    hdr->window   = htons(TCP_RX_BUF);
    hdr->checksum = 0;
    hdr->urgent   = 0;

    for (int i = 0; i < dlen; i++) buf[20 + i] = data[i];

    hdr->checksum = htons(tcp_cksum(net_ip, conn->remote_ip, buf, seglen));
    return ip_send(conn->remote_ip, IP_PROTO_TCP, buf, seglen);
}

// ---- API pública ----

int tcp_connect(TcpConn *conn, const uint8_t ip[4],
                uint16_t port, uint32_t timeout_ms) {
    for (int i = 0; i < 4; i++) conn->remote_ip[i] = ip[i];
    conn->remote_port = port;
    conn->local_port  = next_port++;
    conn->snd_seq     = timer_get_ticks() * 7919 + 1;
    conn->rcv_ack     = 0;
    conn->state       = TCP_STATE_CLOSED;
    conn->remote_fin  = 0;
    conn->rx_len      = 0;
    g_conn            = conn;

    // Envia SYN
    conn->state = TCP_STATE_SYN_SENT;
    tcp_send_seg(conn, TF_SYN, 0, 0);
    conn->snd_seq++;    // SYN consome 1 byte de seq
    terminal_writestring("SYN enviado\n");

    // Aguarda SYN-ACK
    uint32_t t0 = timer_get_ticks();
    while (timer_get_ticks() - t0 < timeout_ms) {
        net_poll();
        if (conn->state == TCP_STATE_ESTABLISHED) return 1;
        if (conn->state == TCP_STATE_CLOSED)      return 0;
    }
    conn->state = TCP_STATE_CLOSED;
    g_conn = 0;
    return 0;
}

int tcp_send(TcpConn *conn, const uint8_t *data, uint16_t len) {
    if (conn->state != TCP_STATE_ESTABLISHED) return -1;
    if (tcp_send_seg(conn, TF_PSH | TF_ACK, data, len) < 0) return -1;
    conn->snd_seq += len;
    return len;
}

int tcp_recv(TcpConn *conn, uint8_t *buf, uint16_t maxlen,
             uint32_t timeout_ms) {
    uint32_t t0 = timer_get_ticks();
    while (timer_get_ticks() - t0 < timeout_ms) {
        net_poll();
        if (conn->rx_len > 0) {
            uint16_t n = (uint16_t)(conn->rx_len > maxlen
                                    ? maxlen : conn->rx_len);
            for (int i = 0; i < n; i++) buf[i] = conn->rx_buf[i];
            // Move dados restantes para o início
            for (uint32_t i = n; i < conn->rx_len; i++)
                conn->rx_buf[i - n] = conn->rx_buf[i];
            conn->rx_len -= n;
            return n;
        }
        if (conn->remote_fin && conn->rx_len == 0) return 0;
    }
    return -1;
}

void tcp_close(TcpConn *conn) {
    if (conn->state == TCP_STATE_ESTABLISHED ||
        conn->state == TCP_STATE_CLOSE_WAIT) {
        uint8_t flags = TF_FIN | TF_ACK;
        tcp_send_seg(conn, flags, 0, 0);
        conn->snd_seq++;
        conn->state = (conn->state == TCP_STATE_CLOSE_WAIT)
                    ? TCP_STATE_LAST_ACK
                    : TCP_STATE_FIN_WAIT_1;
    }
    // Aguarda FIN-ACK por até 2 segundos
    uint32_t t0 = timer_get_ticks();
    while (timer_get_ticks() - t0 < 2000) {
        net_poll();
        if (conn->state == TCP_STATE_CLOSED) break;
    }
    conn->state = TCP_STATE_CLOSED;
    g_conn = 0;
}

// ---- Recepção ----

void tcp_handle(const uint8_t *src_ip, const uint8_t *data, uint16_t len) {
    if (!g_conn || len < 20) return;

    const TcpHdr *hdr = (const TcpHdr *)data;
    uint16_t dst_port = htons(hdr->dst_port);
    uint16_t src_port = htons(hdr->src_port);
    uint8_t  flags    = hdr->flags;
    uint32_t seq      = htonl(hdr->seq);
    uint32_t ack      = htonl(hdr->ack_seq);
    uint8_t  hlen     = (uint8_t)((hdr->data_off >> 4) * 4);

    TcpConn *conn = g_conn;

    // Verifica se é para esta conexão
    if (dst_port != conn->local_port)  return;
    if (src_port != conn->remote_port) return;
    for (int i = 0; i < 4; i++)
        if (src_ip[i] != conn->remote_ip[i]) return;

    terminal_writestring("TCP rx flags=");
    terminal_putchar('0' + (flags & 0xF));
    terminal_writestring(" state=");
    terminal_putchar('0' + conn->state);
    terminal_putchar('\n');

    // RST: fecha imediatamente
    if (flags & TF_RST) { conn->state = TCP_STATE_CLOSED; return; }

    uint16_t payload_len = (uint16_t)(len - hlen);
    const uint8_t *payload = data + hlen;

    // ---- Máquina de estados ----
    if (conn->state == TCP_STATE_SYN_SENT) {
        if ((flags & (TF_SYN | TF_ACK)) == (TF_SYN | TF_ACK)) {
            conn->rcv_ack = seq + 1;
            conn->state   = TCP_STATE_ESTABLISHED;
            tcp_send_seg(conn, TF_ACK, 0, 0);   // ACK do SYN-ACK
        }
        return;
    }

    if (conn->state == TCP_STATE_ESTABLISHED ||
        conn->state == TCP_STATE_FIN_WAIT_1  ||
        conn->state == TCP_STATE_FIN_WAIT_2  ||
        conn->state == TCP_STATE_CLOSE_WAIT) {

        // Dados recebidos
        if (payload_len > 0 && seq == conn->rcv_ack) {
            uint32_t space = TCP_RX_BUF - conn->rx_len;
            uint32_t copy  = payload_len < space ? payload_len : space;
            for (uint32_t i = 0; i < copy; i++)
                conn->rx_buf[conn->rx_len + i] = payload[i];
            conn->rx_len  += copy;
            conn->rcv_ack += payload_len;
            tcp_send_seg(conn, TF_ACK, 0, 0);
        }

        // ACK do nosso FIN
        if ((flags & TF_ACK) && conn->state == TCP_STATE_FIN_WAIT_1)
            conn->state = TCP_STATE_FIN_WAIT_2;

        // FIN do remoto
        if (flags & TF_FIN) {
            conn->rcv_ack++;
            conn->remote_fin = 1;
            tcp_send_seg(conn, TF_ACK, 0, 0);
            if (conn->state == TCP_STATE_FIN_WAIT_2 ||
                conn->state == TCP_STATE_FIN_WAIT_1)
                conn->state = TCP_STATE_CLOSED;
            else
                conn->state = TCP_STATE_CLOSE_WAIT;
        }
    }

    if (conn->state == TCP_STATE_LAST_ACK) {
        if (flags & TF_ACK) conn->state = TCP_STATE_CLOSED;
    }
}
