// idt.h
#ifndef IDT_H
#define IDT_H

#include <stdint.h>

// Inicializa a IDT, remapeia o PIC e habilita interrupções (sti)
void idt_init(void);

#endif
