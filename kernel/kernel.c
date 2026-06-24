#include "idt.h"
#include "kmalloc.h"
#include "fs.h"
#include "fs_disk.h"
#include "env.h"
#include "tty.h"
#include "terminal.h"
#include "shell.h"
#include "sound.h"
#include "netdev.h"
#include "storage.h"
#include "sb16.h"
#include "adlib.h"
#include "beep.h"
#include "rtl8139.h"
#include "e1000.h"     // ← Intel E1000 (padrão QEMU moderno)
#include "pcnet.h"
#include "ne2000.h"
#include "disk.h"
#include "floppy.h"
#include "net.h"
#include "icmp.h"

void kernel_main(uint32_t fs_lba, uint32_t boot_drive) {
    idt_init();
    kmalloc_init();

    if (boot_drive < 0x80) {
        floppy_register();
    } else {
        disk_register();
    }
    storage_init();

    fs_init();
    fsd_init_at(fs_lba);
    env_init();
    tty_init();

    // Som: SB16 → AdLib → PC Beeper
    sb16_register();
    adlib_register();
    pc_beep_register();
    sound_init();

    // Rede: mais rápida / mais comum primeiro
    //   RTL8139  — simples, estável, testado
    //   E1000    — padrão do QEMU moderno (MMIO, rings 64-bit)
    //   PCnet    — padrão QEMU legado / configs antigas
    //   NE2000   — ISA/PCI legacy, último recurso
    rtl8139_register();
    e1000_register();
    pcnet_register();
    ne2000_register();
    netdev_init();

    net_init();
    icmp_init();
    terminal_initialize();

    terminal_writestring("FreeRootSDOS v0.7 Tsar\n");
    terminal_writestring("Som: ");
    terminal_writestring(sound_driver_name());
    terminal_putchar('\n');
    terminal_writestring("Rede: ");
    terminal_writestring(netdev_present() ? netdev_driver_name() : "sem placa");
    terminal_putchar('\n');
    terminal_writestring("Digite 'balloon' para iniciar a interface grafica.\n\n");

    shell_run();
}
