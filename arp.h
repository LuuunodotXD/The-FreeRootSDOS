#ifndef ARP_H
#define ARP_H
#define ARP_TABLE_SIZE  16
#include <stdint.h>

typedef struct { uint8_t ip[4]; uint8_t mac[6]; int valid; } ArpEntry;
extern ArpEntry arp_table[ARP_TABLE_SIZE];

// Resolve IP → MAC (envia request e aguarda reply, timeout em ms)
// Retorna 1 se resolvido, 0 se timeout
int arp_resolve(const uint8_t ip[4], uint8_t mac_out[6], uint32_t timeout_ms);

// Procura só na tabela local (sem enviar request)
int arp_lookup(const uint8_t ip[4], uint8_t mac_out[6]);

// Processa frame ARP recebido (chamado pelo net dispatcher)
void arp_handle(const uint8_t *data, uint16_t len);

// Imprime a tabela ARP no terminal
void arp_print_table(void);

#endif
