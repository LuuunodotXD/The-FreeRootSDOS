// kernel.c
#include "terminal.h"
#include "idt.h"
#include "kmalloc.h"
#include "fs.h"
#include "fs_disk.h"
#include "shell.h"

void kernel_main(void) {
    terminal_initialize();
    idt_init();
    kmalloc_init();
    fs_init();
    fsd_init();
    shell_init();
    terminal_writestring("FreeRootSDOS v0.4 - Kernel iniciado!\n");
    terminal_writestring("Digite 'help' para ver os comandos.\n");
    shell_run();
}
