#ifndef DNS_H
#define DNS_H
#include <stdint.h>

// Resolve hostname → IPv4. Retorna 1 se resolvido, 0 se falha/timeout.
int dns_resolve(const char *hostname, uint8_t ip_out[4], uint32_t timeout_ms);

#endif
