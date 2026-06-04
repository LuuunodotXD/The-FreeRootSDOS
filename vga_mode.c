// vga_mode.c
// Manipulação de modos VGA por hardware (sem BIOS)
// Apenas entrada nos modos gráficos. A saída é feita via reboot, que
// restaura o modo texto original através do bootloader.

#include "vga_mode.h"
#include "io.h"
#include <stdint.h>

// ----------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------

static void crtc_unlock(void) {
    outb(0x3D4, 0x11);
    outb(0x3D5, inb(0x3D5) & ~0x80);
}

static void write_regs(uint16_t port, const uint8_t *data, int count) {
    for (int i = 0; i < count; i++) {
        outb(port, (uint8_t)i);
        outb(port + 1, data[i]);
    }
}

// ----------------------------------------------------------------
// Modo gráfico 12h – 640×480, 16 cores planar
// ----------------------------------------------------------------
void vga_set_mode12h(void) {
    // Miscellaneous Output
    outb(0x3C2, 0xE3);

    // Sequencer
    static const uint8_t seq[] = {
        0x01, 0x01, 0x0F, 0x00, 0x06
    };
    write_regs(0x3C4, seq, sizeof(seq));

    // CRTC
    crtc_unlock();
    static const uint8_t crtc[] = {
        0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0x0B, 0x3E,
        0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xEA, 0x8C, 0xDF, 0x28, 0x00, 0xE7, 0x04, 0xE3,
        0xFF
    };
    for (int i = 0; i < 25; i++) {
        outb(0x3D4, (uint8_t)i);
        outb(0x3D5, crtc[i]);
    }

    // Graphics Controller
    static const uint8_t gc[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0F, 0xFF
    };
    write_regs(0x3CE, gc, sizeof(gc));

    // Attribute Controller
    static const uint8_t ac[] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x14,0x07,
        0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,
        0x01, 0x00, 0x0F, 0x00, 0x00
    };
    inb(0x3DA); // reset flip-flop
    for (int i = 0; i < 21; i++) {
        outb(0x3C0, (uint8_t)i);
        outb(0x3C0, ac[i]);
    }
    outb(0x3C0, 0x20);
}

// ----------------------------------------------------------------
// Modo gráfico 13h – 320×200, 256 cores (mantido para referência)
// ----------------------------------------------------------------
void vga_set_mode13h(void) {
    outb(0x3C2, 0x63);

    static const uint8_t seq[] = { 0x01, 0x01, 0x0F, 0x00, 0x0E };
    write_regs(0x3C4, seq, sizeof(seq));

    crtc_unlock();
    static const uint8_t crtc[] = {
        0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,
        0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x9C, 0x8E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3,
        0xFF
    };
    for (int i = 0; i < 25; i++) {
        outb(0x3D4, (uint8_t)i);
        outb(0x3D5, crtc[i]);
    }

    static const uint8_t gc[] = { 0x00,0x00,0x00,0x00,0x00,0x40,0x05,0x0F,0xFF };
    write_regs(0x3CE, gc, sizeof(gc));

    static const uint8_t ac[] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
        0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
        0x41, 0x00, 0x0F, 0x00, 0x00
    };
    inb(0x3DA);
    for (int i = 0; i < 21; i++) {
        outb(0x3C0, (uint8_t)i);
        outb(0x3C0, ac[i]);
    }
    outb(0x3C0, 0x20);
}
