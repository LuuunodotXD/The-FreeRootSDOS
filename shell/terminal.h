#ifndef TERMINAL_H
#define TERMINAL_H

#include <stdint.h>

void    terminal_initialize(void);
void    terminal_resume(void);      // restaura cursor após troca de modo VGA
void    terminal_writestring(const char *str);
void    terminal_putchar(char c);
void    terminal_clear(void);
void    terminal_update_cursor(void);
void    terminal_set_fg(uint8_t fg);
void    terminal_set_bg(uint8_t bg);
uint8_t terminal_get_fg(void);

// Move o cursor de hardware N posições para esquerda/direita
// sem apagar ou escrever nada — usado pelo shell para Home/End/setas
void    terminal_cursor_left(int n);
void    terminal_cursor_right(int n);
void    terminal_goto(int x, int y);
void    terminal_clear_row(int y);
void    terminal_write_at(int x, int y, uint8_t color, const char *str);
uint8_t terminal_get_bg(void);

// Hook de redirecionamento de saída (para terminal gráfico Balloon)
typedef void (*term_hook_fn)(char);
void terminal_set_hook(term_hook_fn fn);

#endif
