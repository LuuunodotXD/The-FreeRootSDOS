#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

// Caracteres normais retornam ASCII (< 0x80)
// Teclas especiais retornam estas constantes (>= 0x80, como char com sinal seria negativo,
// então getchar retorna int para poder distinguir)
#define KEY_LEFT    0x80
#define KEY_RIGHT   0x81
#define KEY_HOME    0x82
#define KEY_END     0x83
#define KEY_DEL     0x84
#define KEY_UP      0x85
#define KEY_DOWN    0x86

int  getchar(void);           // bloqueia ate ter dado
int  keyboard_available(void); // 1 se ha tecla no buffer, 0 se nao

void keyboard_irq(void);

#endif
