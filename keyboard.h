#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

// Teclas especiais (>= 0x80)
#define KEY_LEFT    0x80
#define KEY_RIGHT   0x81
#define KEY_HOME    0x82
#define KEY_END     0x83
#define KEY_DEL     0x84
#define KEY_UP      0x85
#define KEY_DOWN    0x86

#define KEY_PGUP    0x87
#define KEY_PGDN    0x88

// Ctrl+letra retorna o código de controle ASCII (1-26)
// Constantes nomeadas para os mais usados
#define KEY_CTRL_S  0x13   // Ctrl+S = 19
#define KEY_CTRL_Q  0x11   // Ctrl+Q = 17
#define KEY_CTRL_D  0x04   // Ctrl+D = 4  (delete line)
#define KEY_ESC     0x1B   // Esc = 27

int  getchar(void);
int  keyboard_available(void);
void keyboard_irq(void);

#endif
