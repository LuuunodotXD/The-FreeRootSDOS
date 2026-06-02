// mouse.h — driver PS/2 para modo gráfico
#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

#define MOUSE_BTN_LEFT   1
#define MOUSE_BTN_RIGHT  2
#define MOUSE_BTN_MID    4

void mouse_init(void);
void mouse_irq(void);       // chamado pelo stub IRQ12 em idt.c

int  mouse_x(void);
int  mouse_y(void);
int  mouse_buttons(void);
int  mouse_moved(void);     // retorna 1 se posição mudou, zera a flag

#endif
