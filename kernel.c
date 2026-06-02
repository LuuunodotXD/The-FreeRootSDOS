// kernel.c
#include "idt.h"
#include "kmalloc.h"
#include "disk.h"
#include "fs.h"
#include "fs_disk.h"
#include "env.h"
#include "tty.h"
#include "terminal.h"
#include "shell.h"

void kernel_main(void) {
    idt_init();
    kmalloc_init();
    disk_init();
    fs_init();
    fsd_init();
    env_init();
    tty_init();
    terminal_initialize();

    terminal_writestring("FreeRootSDOS v0.5\n");
    terminal_writestring("Digite 'balloon' para iniciar a interface grafica.\n");
    terminal_writestring("Digite 'help' para ver os comandos.\n\n");

    shell_run();  // nunca retorna
}
