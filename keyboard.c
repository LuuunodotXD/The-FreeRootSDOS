// keyboard.c
#include "keyboard.h"

static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "dN"(port));
    return ret;
}

// Tabela de scancode (press) para ASCII
static const char scancode_to_ascii[] = {
    0,   0,   '1', '2', '3', '4', '5', '6', '7', '8',  // 00-09
    '9', '0', '-', '=', '\b',   0,   'q', 'w', 'e', 'r', // 0A-13 (0E = backspace)
    't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',  0,    // 14-1D
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',    // 1E-27
    '\'', 0,   0, '\\', 'z', 'x', 'c', 'v', 'b', 'n',    // 28-31
    'm', ',', '.', '/',  0,   '*',  0,  ' '               // 32-39
};

char getchar(void) {
    unsigned char scancode;

    while (1) {
        // Aguarda dado disponível (bit 0 da porta 0x64)
        while ((inb(0x64) & 0x01) == 0);

        scancode = inb(0x60);

        // Ignora key-release (bit 7 set) e scancodes sem mapeamento
        if (scancode & 0x80)
            continue;

        if (scancode < sizeof(scancode_to_ascii))
            return scancode_to_ascii[scancode];
        // Scancode sem mapeamento: continua esperando
    }
}
