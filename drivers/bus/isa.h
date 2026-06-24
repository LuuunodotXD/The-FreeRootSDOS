#ifndef ISA_H
#define ISA_H
#include <stdint.h>

// Sonda uma lista de portas de I/O candidatas, chamando probe_fn para cada uma.
// probe_fn deve retornar 1 se o dispositivo foi detectado naquele endereço
// (e pode ter efeito colateral, como guardar o MAC encontrado).
// Retorna o I/O base que bateu, ou 0 se nenhum funcionou.
typedef int (*isa_probe_fn)(uint16_t io_base);
uint16_t isa_probe_ports(const uint16_t *candidates, int count, isa_probe_fn probe_fn);

// ---- Reserva de recursos (evita dois drivers ISA brigarem pelo mesmo IRQ/DMA) ----
int  isa_irq_reserve(int irq);      // 1 = conseguiu, 0 = já em uso
void isa_irq_release(int irq);
int  isa_dma_reserve(int channel);  // canais DMA 8-bit: 0-3, 16-bit: 5-7
void isa_dma_release(int channel);

#endif
