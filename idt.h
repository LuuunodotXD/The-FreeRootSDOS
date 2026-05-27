#ifndef IDT_H
#define IDT_H

#include <stdint.h>
#include "terminal.h"   // isr_handler usa terminal_set_fg/bg/clear/writestring

void     idt_init(void);
uint32_t timer_get_ticks(void);    // ticks desde o boot (1 tick = 1 ms)
void     timer_sleep(uint32_t ms); // bloqueia por N milissegundos

#endif
