// terminal.c
#include <stdint.h>
#include "terminal.h"
#include "io.h"

#define VGA_ADDRESS 0xB8000
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

static uint16_t* vga_buffer = (uint16_t*)VGA_ADDRESS;
static int cursor_x = 0, cursor_y = 0;
static uint8_t terminal_color = 0x0F; // Branco sobre preto

// Função para atualizar a posição do cursor de hardware
void terminal_update_cursor(void) {
    uint16_t pos = cursor_y * VGA_WIDTH + cursor_x;
    outb(0x3D4, 14);            // índice do registrador high
    outb(0x3D5, (pos >> 8) & 0xFF);
    outb(0x3D4, 15);            // índice do registrador low
    outb(0x3D5, pos & 0xFF);
}

void terminal_putchar(char c) {
    // Trata caracteres de controle
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\b') {
        // Backspace: move o cursor para trás e apaga o caractere
        if (cursor_x > 0) {
            cursor_x--;
        } else if (cursor_y > 0) {
            cursor_x = VGA_WIDTH - 1;
            cursor_y--;
        } else {
            // No início da tela: não faz nada
            terminal_update_cursor();
            return;
        }
        // Escreve um espaço na posição atual para apagar
        vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = (terminal_color << 8) | ' ';
        // Atualiza o cursor e retorna (não avança)
        terminal_update_cursor();
        return;
    } else {
        // Caractere normal imprimível
        vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = (terminal_color << 8) | c;
        cursor_x++;
        if (cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }
    }

    // Rolagem se necessário
    if (cursor_y >= VGA_HEIGHT) {
        for (int i = 1; i < VGA_HEIGHT; i++) {
            for (int j = 0; j < VGA_WIDTH; j++) {
                vga_buffer[(i-1)*VGA_WIDTH + j] = vga_buffer[i*VGA_WIDTH + j];
            }
        }
        for (int j = 0; j < VGA_WIDTH; j++) {
            vga_buffer[(VGA_HEIGHT-1)*VGA_WIDTH + j] = (terminal_color << 8) | ' ';
        }
        cursor_y = VGA_HEIGHT - 1;
    }

    // Atualiza o cursor hardware para a nova posição
    terminal_update_cursor();
}

void terminal_writestring(const char* str) {
    while (*str) {
        terminal_putchar(*str++);
    }
}

void terminal_initialize() {
    for (int y = 0; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] = (terminal_color << 8) | ' ';
        }
    }
    cursor_x = 0;
    cursor_y = 0;
    terminal_update_cursor();
}

void terminal_clear() {
    for (int y = 0; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] = (terminal_color << 8) | ' ';
        }
    }
    cursor_x = 0;
    cursor_y = 0;
    terminal_update_cursor();
}
