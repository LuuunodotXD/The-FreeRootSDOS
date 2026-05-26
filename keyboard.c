// keyboard.c
#include <stdint.h>
#include "keyboard.h"
#include "io.h"

// ----------------------------------------------------------------
// Ring buffer — preenchido pelo IRQ1, consumido pelo getchar()
// ----------------------------------------------------------------

#define KBUF_SIZE 64

static volatile char kbuf[KBUF_SIZE];
static volatile int  kbuf_head = 0;   // próxima posição de escrita
static volatile int  kbuf_tail = 0;   // próxima posição de leitura

static const char scancode_to_ascii[] = {
    0,    0,   '1', '2', '3', '4', '5', '6', '7', '8',
    '9', '0',  '-', '=', '\b', 0,  'q', 'w', 'e', 'r',
    't', 'y',  'u', 'i', 'o', 'p', '[', ']', '\n', 0,
    'a', 's',  'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
    '\'', 0,    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n',
    'm', ',',  '.', '/',  0,  '*',  0,  ' '
};

// Chamado pelo irq1_stub em idt.c
void keyboard_irq(void) {
    uint8_t sc = inb(0x60);

    if (sc & 0x80) {
        // Key release — descarta e envia EOI
        outb(0x20, 0x20);
        return;
    }

    if (sc < sizeof(scancode_to_ascii)) {
        char c = scancode_to_ascii[sc];
        if (c) {
            int next = (kbuf_head + 1) % KBUF_SIZE;
            if (next != kbuf_tail) {   // buffer não cheio
                kbuf[kbuf_head] = c;
                kbuf_head = next;
            }
        }
    }

    outb(0x20, 0x20);   // EOI para o PIC master
}

// Bloqueia até ter um caractere no buffer (IRQ faz o trabalho)
char getchar(void) {
    while (kbuf_head == kbuf_tail)
        asm volatile ("hlt");   // dorme até próxima interrupção

    char c = kbuf[kbuf_tail];
    kbuf_tail = (kbuf_tail + 1) % KBUF_SIZE;
    return c;
}
