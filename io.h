// io.h
#ifndef IO_H
#define IO_H

static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "dN"(port));
    return ret;
}

static inline void outb(unsigned short port, unsigned char data) {
    asm volatile ("outb %1, %0" : : "dN"(port), "a"(data));
}

#endif
