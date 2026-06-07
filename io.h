// io.h
#ifndef IO_H
#define IO_H

#include <stdint.h>

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "dN"(port));
    return ret;
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile ("inw %1, %0" : "=a"(ret) : "dN"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t data) {
    asm volatile ("outb %1, %0" : : "dN"(port), "a"(data));
}

static inline void outw(uint16_t port, uint16_t data) {
    asm volatile ("outw %1, %0" : : "dN"(port), "a"(data));
}

static inline void io_wait(void) {
    outb(0x80, 0x00);
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    asm volatile ("inl %1, %0" : "=a"(ret) : "dN"(port));
    return ret;
}
static inline void outl(uint16_t port, uint32_t data) {
    asm volatile ("outl %1, %0" : : "dN"(port), "a"(data));
}

#endif
