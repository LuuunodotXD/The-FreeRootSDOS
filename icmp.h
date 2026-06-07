#ifndef ICMP_H
#define ICMP_H
#include <stdint.h>

void icmp_init(void);
// Retorna RTT em ms ou -1 em timeout
void icmp_flush(void);
int  icmp_ping(const uint8_t ip[4], uint16_t seq, uint32_t timeout_ms);
void icmp_handle(const uint8_t *ip_src, const uint8_t *data, uint16_t len);
#endif
