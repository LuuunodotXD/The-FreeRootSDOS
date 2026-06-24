#ifndef UDP_H
#define UDP_H
#include <stdint.h>

// Callback chamado quando chega um pacote UDP na porta registrada
// src_ip: IP de origem, src_port: porta de origem, data/len: payload
typedef void (*udp_rx_cb)(const uint8_t src_ip[4], uint16_t src_port,
                           const uint8_t *data, uint16_t len);

// Registra callback para uma porta local (0 = desregistra)
void udp_listen(uint16_t port, udp_rx_cb cb);

// Envia pacote UDP
int  udp_send(const uint8_t dst_ip[4], uint16_t dst_port, uint16_t src_port,
              const uint8_t *data, uint16_t len);

// Processa frame UDP recebido (chamado pelo ip.c)
void udp_handle(const uint8_t *src_ip, const uint8_t *data, uint16_t len);

#endif
