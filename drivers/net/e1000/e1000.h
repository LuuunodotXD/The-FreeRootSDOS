#ifndef E1000_H
#define E1000_H
#include <stdint.h>

// Detecta e inicializa o E1000 via PCI.
// Testa 0x8086:0x100E (82540EM) e 0x8086:0x100F (82545EM).
// Retorna 0 se encontrado, -1 se ausente.
int  e1000_init(void);

// Retorna 1 se e1000_init() foi bem-sucedido.
int  e1000_present(void);

// Envia um frame Ethernet cru. len deve ser 14–2048 bytes.
int  e1000_send(const uint8_t *data, uint16_t len);

// Verifica descriptors RX e entrega frames ao callback registrado.
void e1000_poll(void);

// Copia o MAC da placa para out[6].
void e1000_get_mac(uint8_t out[6]);

// Registra callback chamado a cada frame recebido.
void e1000_set_rx_callback(void (*cb)(const uint8_t *data, uint16_t len));

// Registra o driver no HAL de rede.
// Deve ser chamado entre rtl8139_register() e pcnet_register().
void e1000_register(void);

#endif
