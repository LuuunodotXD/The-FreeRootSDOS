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
#define KEY_CTRL_D  0x04
#define KEY_ESC     0x1B

// Alt+Fn (terminais 0-7)
#define KEY_ALT_F1   0xA0
#define KEY_ALT_F2   0xA1
#define KEY_ALT_F3   0xA2
#define KEY_ALT_F4   0xA3
#define KEY_ALT_F5   0xA4
#define KEY_ALT_F6   0xA5
#define KEY_ALT_F7   0xA6
#define KEY_ALT_F8   0xA7
// Ctrl+Alt+Fn (terminais 8-F)
#define KEY_CTRL_ALT_F1  0xB0
#define KEY_CTRL_ALT_F2  0xB1
#define KEY_CTRL_ALT_F3  0xB2
#define KEY_CTRL_ALT_F4  0xB3
#define KEY_CTRL_ALT_F5  0xB4
#define KEY_CTRL_ALT_F6  0xB5
#define KEY_CTRL_ALT_F7  0xB6
#define KEY_CTRL_ALT_F8  0xB7

int  getchar(void);
int  getchar_nonblock(void);
int  keyboard_available(void);
void keyboard_irq(void);

#endif
