// terminal.c
#include <stdint.h>
#include "terminal.h"
#include "io.h"

#define VGA_ADDRESS 0xB8000
#define VGA_WIDTH   80
#define VGA_HEIGHT  25

static uint16_t *vga_buffer = (uint16_t*)VGA_ADDRESS;
static int      cursor_x = 0, cursor_y = 0;
static uint8_t  fg_color = 0xF;   // branco
static uint8_t  bg_color = 0x0;   // preto

static uint8_t current_color(void) {
    return (bg_color << 4) | (fg_color & 0xF);
}

void terminal_set_fg(uint8_t fg) { fg_color = fg & 0xF; }
void terminal_set_bg(uint8_t bg) { bg_color = bg & 0xF; }
uint8_t terminal_get_fg(void)    { return fg_color; }

static void vga_disable_blink(void) {
    inb(0x3DA);
    outb(0x3C0, 0x10 | 0x20);
    uint8_t val = inb(0x3C1);
    val &= ~0x08;
    outb(0x3C0, val);
    outb(0x3C0, 0x20);
}

void terminal_update_cursor(void) {
    uint16_t pos = cursor_y * VGA_WIDTH + cursor_x;
    outb(0x3D4, 14);
    outb(0x3D5, (pos >> 8) & 0xFF);
    outb(0x3D4, 15);
    outb(0x3D5, pos & 0xFF);
}

void terminal_putchar(char c) {
    uint8_t color = current_color();

    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
        } else if (cursor_y > 0) {
            cursor_x = VGA_WIDTH - 1;
            cursor_y--;
        } else {
            terminal_update_cursor();
            return;
        }
        vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = (color << 8) | ' ';
        terminal_update_cursor();
        return;
    } else {
        vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = (color << 8) | (unsigned char)c;
        if (++cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }
    }

    if (cursor_y >= VGA_HEIGHT) {
        for (int i = 1; i < VGA_HEIGHT; i++)
            for (int j = 0; j < VGA_WIDTH; j++)
                vga_buffer[(i-1)*VGA_WIDTH + j] = vga_buffer[i*VGA_WIDTH + j];
        for (int j = 0; j < VGA_WIDTH; j++)
            vga_buffer[(VGA_HEIGHT-1)*VGA_WIDTH + j] = (color << 8) | ' ';
        cursor_y = VGA_HEIGHT - 1;
    }

    terminal_update_cursor();
}

void terminal_writestring(const char *str) {
    while (*str) terminal_putchar(*str++);
}

void terminal_initialize(void) {
    vga_disable_blink();
    fg_color = 0xF;
    bg_color = 0x0;
    for (int y = 0; y < VGA_HEIGHT; y++)
        for (int x = 0; x < VGA_WIDTH; x++)
            vga_buffer[y * VGA_WIDTH + x] = (current_color() << 8) | ' ';
    cursor_x = 0;
    cursor_y = 0;
    terminal_update_cursor();
}

void terminal_clear(void) {
    uint8_t color = current_color();
    for (int y = 0; y < VGA_HEIGHT; y++)
        for (int x = 0; x < VGA_WIDTH; x++)
            vga_buffer[y * VGA_WIDTH + x] = (color << 8) | ' ';
    cursor_x = 0;
    cursor_y = 0;
    terminal_update_cursor();
}
