// idt.c
#include <stdint.h>
#include "idt.h"
#include "io.h"

// ----------------------------------------------------------------
// Estruturas da IDT
// ----------------------------------------------------------------

struct idt_entry {
    uint16_t offset_low;   // bits 0–15 do endereço do handler
    uint16_t selector;     // seletor de código (0x08 = kernel code)
    uint8_t  zero;         // sempre 0
    uint8_t  type_attr;    // tipo + atributos (0x8E = interrupt gate, presente, ring0)
    uint16_t offset_high;  // bits 16–31 do endereço do handler
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr   idtp;

static void idt_set(uint8_t num, uint32_t handler) {
    idt[num].offset_low  = handler & 0xFFFF;
    idt[num].selector    = 0x08;          // seletor do segmento de código
    idt[num].zero        = 0;
    idt[num].type_attr   = 0x8E;          // present=1, DPL=0, tipo=interrupt gate 32-bit
    idt[num].offset_high = (handler >> 16) & 0xFFFF;
}

// ----------------------------------------------------------------
// Remapeamento do PIC 8259
//
// Por padrão, IRQ0–7  disparam INT 0x08–0x0F  (conflito com exceções da CPU)
//             IRQ8–15 disparam INT 0x70–0x77
// Remapeamos para:
//             IRQ0–7  → INT 0x20–0x27
//             IRQ8–15 → INT 0x28–0x2F
// ----------------------------------------------------------------

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

static void pic_remap(void) {
    // Inicia sequência de inicialização (ICW1)
    outb(PIC1_CMD,  0x11); io_wait();
    outb(PIC2_CMD,  0x11); io_wait();

    // ICW2: novos vetores de base
    outb(PIC1_DATA, 0x20); io_wait();  // IRQ0–7  → INT 0x20–0x27
    outb(PIC2_DATA, 0x28); io_wait();  // IRQ8–15 → INT 0x28–0x2F

    // ICW3: cascata
    outb(PIC1_DATA, 0x04); io_wait();  // master: slave em IRQ2
    outb(PIC2_DATA, 0x02); io_wait();  // slave: identidade 2

    // ICW4: modo 8086
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();

    // Mascara todas as IRQs exceto IRQ0 (timer) e IRQ1 (teclado)
    // Bitmask: bit 0 = IRQ0, bit 1 = IRQ1, etc. 0 = habilitado, 1 = mascarado
    outb(PIC1_DATA, 0b11111100);  // habilita só IRQ0 e IRQ1
    outb(PIC2_DATA, 0b11111111);  // todas do slave mascaradas (não usamos)
}

// ----------------------------------------------------------------
// Handlers de IRQ (naked: sem prólogo/epílogo gerado pelo GCC)
// ----------------------------------------------------------------

// Função C chamada pelo handler do timer (IRQ0)
// Apenas envia EOI — base para futura implementação de scheduler/uptime
static void timer_handler(void) {
    outb(PIC1_CMD, PIC_EOI);
}

// Declarada em keyboard.c — handler C chamado pelo stub do IRQ1
void keyboard_irq(void);

static void __attribute__((naked)) irq0_stub(void) {
    asm volatile (
        "pusha\n\t"
        "call timer_handler\n\t"
        "popa\n\t"
        "iret"
    );
}

static void __attribute__((naked)) irq1_stub(void) {
    asm volatile (
        "pusha\n\t"
        "call keyboard_irq\n\t"
        "popa\n\t"
        "iret"
    );
}

// ----------------------------------------------------------------
// Inicialização
// ----------------------------------------------------------------

void idt_init(void) {
    // Zera a tabela toda
    for (int i = 0; i < 256; i++) {
        idt[i].offset_low  = 0;
        idt[i].selector    = 0x08;
        idt[i].zero        = 0;
        idt[i].type_attr   = 0x8E;
        idt[i].offset_high = 0;
    }

    pic_remap();

    // Registra os handlers
    idt_set(0x20, (uint32_t)irq0_stub);   // IRQ0 = timer
    idt_set(0x21, (uint32_t)irq1_stub);   // IRQ1 = teclado

    // Carrega a IDT
    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (uint32_t)&idt;
    asm volatile ("lidt %0" : : "m"(idtp));

    // Habilita interrupções
    asm volatile ("sti");
}
