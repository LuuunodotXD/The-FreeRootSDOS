#include "isa.h"

uint16_t isa_probe_ports(const uint16_t *candidates, int count, isa_probe_fn probe_fn) {
    for (int i = 0; i < count; i++) {
        if (probe_fn(candidates[i])) return candidates[i];
    }
    return 0;
}

#define MAX_IRQ 16
#define MAX_DMA 8

static uint8_t irq_used[MAX_IRQ];
static uint8_t dma_used[MAX_DMA];

int isa_irq_reserve(int irq) {
    if (irq < 0 || irq >= MAX_IRQ) return 0;
    if (irq_used[irq]) return 0;
    irq_used[irq] = 1;
    return 1;
}
void isa_irq_release(int irq) {
    if (irq >= 0 && irq < MAX_IRQ) irq_used[irq] = 0;
}

int isa_dma_reserve(int channel) {
    if (channel < 0 || channel >= MAX_DMA) return 0;
    if (dma_used[channel]) return 0;
    dma_used[channel] = 1;
    return 1;
}
void isa_dma_release(int channel) {
    if (channel >= 0 && channel < MAX_DMA) dma_used[channel] = 0;
}
