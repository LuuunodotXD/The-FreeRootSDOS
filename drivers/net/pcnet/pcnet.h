#ifndef PCNET_H
#define PCNET_H
#include <stdint.h>

// Detecta e inicializa a placa via PCI (vendor 0x1022, device 0x2000).
// Retorna 0 se encontrada, -1 se ausente.
int  pcnet_init(void);

// Retorna 1 se pcnet_init() foi bem-sucedido.
int  pcnet_present(void);

// Envia um frame Ethernet cru. len deve ser 14–1514 bytes.
int  pcnet_send(const uint8_t *data, uint16_t len);

// Verifica descriptors RX e entrega frames ao callback registrado.
// Deve ser chamado periodicamente (no loop da shell ou do netdev).
void pcnet_poll(void);

// Copia o MAC da placa para out[6].
void pcnet_get_mac(uint8_t out[6]);

// Registra callback chamado a cada frame recebido.
void pcnet_set_rx_callback(void (*cb)(const uint8_t *data, uint16_t len));

// Registra o driver no HAL de rede.
// Deve ser chamado entre rtl8139_register() e ne2000_register().
void pcnet_register(void);

#endif
