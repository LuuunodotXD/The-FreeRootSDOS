#ifndef TCP_H
#define TCP_H
#include <stdint.h>

#define TCP_STATE_CLOSED      0
#define TCP_STATE_SYN_SENT    1
#define TCP_STATE_ESTABLISHED 2
#define TCP_STATE_FIN_WAIT_1  3
#define TCP_STATE_FIN_WAIT_2  4
#define TCP_STATE_CLOSE_WAIT  5
#define TCP_STATE_LAST_ACK    6

#define TCP_RX_BUF  8192

typedef struct {
    uint8_t  remote_ip[4];
    uint16_t remote_port;
    uint16_t local_port;
    uint32_t snd_seq;       // próximo seq a enviar
    uint32_t rcv_ack;       // próximo seq esperado do remoto
    int      state;
    int      remote_fin;    // recebeu FIN do remoto

    uint8_t  rx_buf[TCP_RX_BUF];
    uint32_t rx_len;
} TcpConn;

// Conecta. Retorna 1 se OK, 0 se timeout/falha.
int  tcp_connect(TcpConn *conn, const uint8_t ip[4],
                 uint16_t port, uint32_t timeout_ms);

// Envia dados. Retorna bytes enviados ou -1.
int  tcp_send(TcpConn *conn, const uint8_t *data, uint16_t len);

// Recebe dados disponíveis. Retorna bytes, 0 se fechado, -1 se timeout.
int  tcp_recv(TcpConn *conn, uint8_t *buf, uint16_t maxlen,
              uint32_t timeout_ms);

// Fecha a conexão.
void tcp_close(TcpConn *conn);

// Chamado pelo ip_handle para cada segmento TCP.
void tcp_handle(const uint8_t *src_ip, const uint8_t *data, uint16_t len);

#endif
