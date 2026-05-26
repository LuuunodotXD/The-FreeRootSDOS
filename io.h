// io.h
#ifndef IO_H
#define IO_H

#include <stdint.h>

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "dN"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t data) {
    asm volatile ("outb %1, %0" : : "dN"(port), "a"(data));
}

// Necessário para o poweroff via ACPI/QEMU (porta de 16 bits)
static inline void outw(uint16_t port, uint16_t data) {
    asm volatile ("outw %1, %0" : : "dN"(port), "a"(data));
}

// Pequeno delay de I/O (escreve numa porta inócua)
static inline void io_wait(void) {
    outb(0x80, 0x00);
}

#endif
