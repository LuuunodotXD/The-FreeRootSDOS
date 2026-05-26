#ifndef TERMINAL_H
#define TERMINAL_H

#include <stdint.h>

void    terminal_initialize(void);
void    terminal_writestring(const char *str);
void    terminal_putchar(char c);
void    terminal_clear(void);
void    terminal_update_cursor(void);
void    terminal_set_fg(uint8_t fg);   // 0x0–0xF
void    terminal_set_bg(uint8_t bg);   // 0x0–0xF
uint8_t terminal_get_fg(void);         // retorna cor atual do texto

#endif
