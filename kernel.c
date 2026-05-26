// kernel.c
#include "terminal.h"
#include "idt.h"
#include "shell.h"

void kernel_main(void) {
    terminal_initialize();
    idt_init();     // configura IDT, remapeia PIC, habilita interrupções (sti)
    shell_init();   // registra segundos de boot para o uptime
    terminal_writestring("FreeRootSDOS v0.2 - Kernel iniciado!\n");
    terminal_writestring("Digite 'help' para ver os comandos.\n");
    shell_run();
}
