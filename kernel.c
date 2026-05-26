// kernel.c
#include "terminal.h"
#include "shell.h"

void kernel_main(void) {
    // Marcador direto na VGA — aparece ANTES de qualquer init
    // Coluna 3: 'M' = kernel_main foi alcancado
    volatile unsigned short *vga = (volatile unsigned short*)0xB8000;
    vga[3] = 0x0F4D;   /* 'M' branco */

    terminal_initialize();
    terminal_writestring("FreeRootSDOS v0.1 - Kernel iniciado com sucesso!\n");
    terminal_writestring("Digite 'help' para ver os comandos.\n");
    terminal_writestring("\n> ");
    shell_run();
}
