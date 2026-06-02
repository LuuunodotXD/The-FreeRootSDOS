// vga_mode.c — troca de modo VGA por escrita direta nas portas (sem BIOS)
// Permite chamar em modo protegido, onde int 0x10 não está disponível.
//
// vga_set_mode13h() : 320×200, 256 cores (modo gráfico, framebuffer em 0xA0000)
// vga_set_mode03h() : 80×25 texto (modo texto, buffer em 0xB8000)
//
// A paleta DAC é preservada entre as trocas; as 16 primeiras entradas
// (cores padrão VGA) ficam intactas desde o boot.

#include "vga_mode.h"
#include "font8x8.h"
#include "io.h"
#include <stdint.h>

// ----------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------

// Desabilita blink (bit 3 do Attribute Mode Control)
static void ac_disable_blink(void) {
    inb(0x3DA);               // reset flip-flop ATC
    outb(0x3C0, 0x10 | 0x20); // índice 0x10, acesso desabilitado
    uint8_t v = inb(0x3C1);
    outb(0x3C0, v & ~0x08);   // limpa bit de blink
    outb(0x3C0, 0x20);        // habilita vídeo
}

// Escreve sequência de registradores indexados (porta index, porta data)
static void write_regs(uint16_t port, const uint8_t *data, int count) {
    for (int i = 0; i < count; i++) {
        outb(port,     (uint8_t)i);
        outb(port + 1, data[i]);
    }
}

// Desbloqueia escrita nos registradores 0-7 do CRTC
static void crtc_unlock(void) {
    outb(0x3D4, 0x11);
    outb(0x3D5, inb(0x3D5) & ~0x80);
}

// Escreve registradores do Attribute Controller
// (requer reset do flip-flop antes de cada sequência)
static void write_ac(const uint8_t *data, int count) {
    inb(0x3DA); // reset flip-flop
    for (int i = 0; i < count; i++) {
        outb(0x3C0, (uint8_t)i); // índice
        outb(0x3C0, data[i]);    // valor
    }
    outb(0x3C0, 0x20); // habilita exibição
}

// ----------------------------------------------------------------
// Modo 13h — 320×200, 256 cores
// (framebuffer linear em 0xA0000, 1 byte por pixel)
// ----------------------------------------------------------------
void vga_set_mode13h(void) {
    // Miscellaneous Output: 25 MHz clock, CRTC em 0x3D4
    outb(0x3C2, 0x63);

    // Sequencer: reset síncrono, depois configura
    outb(0x3C4, 0x00); outb(0x3C5, 0x01); // reset assíncrono
    outb(0x3C4, 0x01); outb(0x3C5, 0x01); // clocking mode: 8 dots/char
    outb(0x3C4, 0x02); outb(0x3C5, 0x0F); // map mask: todos os planos
    outb(0x3C4, 0x03); outb(0x3C5, 0x00); // char map select
    outb(0x3C4, 0x04); outb(0x3C5, 0x0E); // memory mode: chain-4
    outb(0x3C4, 0x00); outb(0x3C5, 0x03); // fim de reset

    // CRTC
    crtc_unlock();
    static const uint8_t crtc[] = {
        0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F, // 0x00-0x07
        0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x08-0x0F
        0x9C, 0x8E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3, // 0x10-0x17
        0xFF                                              // 0x18
    };
    for (int i = 0; i < (int)(sizeof crtc); i++) {
        outb(0x3D4, (uint8_t)i);
        outb(0x3D5, crtc[i]);
    }

    // Graphics Controller
    static const uint8_t gc[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF
    };
    for (int i = 0; i < (int)(sizeof gc); i++) {
        outb(0x3CE, (uint8_t)i);
        outb(0x3CF, gc[i]);
    }

    // Attribute Controller (paleta 0-15 + mode control)
    static const uint8_t ac[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x41, // índice 0x10: mode control (graphics, 256 cores)
        0x00, // índice 0x11: overscan
        0x0F, // índice 0x12: color plane enable
        0x00, // índice 0x13: h panning
        0x00  // índice 0x14: color select
    };
    write_ac(ac, sizeof ac);
}

// ----------------------------------------------------------------
// Modo 03h — 80×25 texto
// (buffer VGA em 0xB8000, fonte em plano 2)
// ----------------------------------------------------------------
void vga_set_mode03h(void) {
    // Miscellaneous Output: 25 MHz, modo positivo V/H sync
    outb(0x3C2, 0x67);

    // Sequencer
    outb(0x3C4, 0x00); outb(0x3C5, 0x01); // reset
    outb(0x3C4, 0x01); outb(0x3C5, 0x00); // clocking mode: 9 dots/char
    outb(0x3C4, 0x02); outb(0x3C5, 0x03); // map mask: planos 0+1 (chars)
    outb(0x3C4, 0x03); outb(0x3C5, 0x00); // char map select: fonte A = fonte B = slot 0
    outb(0x3C4, 0x04); outb(0x3C5, 0x02); // memory mode: odd/even (texto)
    outb(0x3C4, 0x00); outb(0x3C5, 0x03); // fim de reset

    // CRTC
    crtc_unlock();
    static const uint8_t crtc[] = {
        0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F, // 0x00-0x07
        0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x00, // 0x08-0x0F
        0x9C, 0x8E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3, // 0x10-0x17
        0xFF                                              // 0x18
    };
    for (int i = 0; i < (int)(sizeof crtc); i++) {
        outb(0x3D4, (uint8_t)i);
        outb(0x3D5, crtc[i]);
    }

    // Graphics Controller (odd/even para texto)
    static const uint8_t gc[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF
    };
    for (int i = 0; i < (int)(sizeof gc); i++) {
        outb(0x3CE, (uint8_t)i);
        outb(0x3CF, gc[i]);
    }

    // Attribute Controller (paleta CGA 16 cores + modo texto)
    static const uint8_t ac[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
        0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
        0x0C, // índice 0x10: mode control (texto)
        0x00, // índice 0x11: overscan
        0x0F, // índice 0x12: color plane enable
        0x08, // índice 0x13: h panning
        0x00  // índice 0x14: color select
    };
    write_ac(ac, sizeof ac);

    ac_disable_blink();

    // Recarrega a fonte: o modo 13h usa chain-4 e sobrescreve o plano 2
    // (onde a VGA armazena os bitmaps dos caracteres). Sem isso o terminal
    // volta com letras corrompidas.
    vga_load_font_8x8();
}

// ----------------------------------------------------------------
// Recarga da fonte 8×8 no plano 2 após corrupção pelo modo 13h
//
// Em chain-4 (modo 13h) cada byte escrito em 0xA0000+N vai para o
// plano (N % 4) — incluindo o plano 2, onde fica a fonte de texto.
// Após vga_set_mode03h() é preciso regenerar esses dados.
//
// Protocolo:
//   1. Sequencer: acesso sequencial (desliga chain-4 e odd/even)
//   2. GC: mapeamento linear em 0xA0000 para escrita no plano 2
//   3. Escreve 256 slots de 32 bytes (VGA aloca 32 bytes/char mesmo
//      com fonte de 8 linhas; o CRTC lê os primeiros max_scan_line+1)
//   4. Restaura sequencer/GC para modo texto normal
// ----------------------------------------------------------------
void vga_load_font_8x8(void) {
    // 1. Habilita escrita exclusiva no plano 2
    outb(0x3C4, 0x00); outb(0x3C5, 0x01); // reset síncrono
    outb(0x3C4, 0x02); outb(0x3C5, 0x04); // map mask: somente plano 2
    outb(0x3C4, 0x04); outb(0x3C5, 0x06); // memory mode: extended, sem chain4/odd-even
    outb(0x3C4, 0x00); outb(0x3C5, 0x03); // fim de reset

    // 2. GC: acesso linear ao plano 2 (área 0xA0000–0xAFFFF)
    outb(0x3CE, 0x04); outb(0x3CF, 0x02); // read map select: plano 2
    outb(0x3CE, 0x05); outb(0x3CF, 0x00); // mode: write mode 0, read mode 0
    outb(0x3CE, 0x06); outb(0x3CF, 0x04); // misc: mapa em 0xA0000, sem odd/even

    // 3. Escreve bitmaps no plano 2
    //    VGA aloca 32 bytes por caractere; nossa fonte tem 8 linhas.
    //    As linhas 8–15 ficam zeradas (célula de 16px, cursor em linha 13).
    //
    //    font8x8_data usa LSB = pixel mais à esquerda (como vga13h_char o lê),
    //    mas o hardware de texto VGA espera MSB = pixel mais à esquerda.
    //    Por isso cada byte precisa ter seus bits revertidos.
    // O CRTC está configurado com célula de 16 linhas (max_scan_line = 15).
    // Para a fonte 8×8 preencher a célula inteira, cada linha do glifo é
    // repetida duas vezes (scale 8→16). Isso elimina o aspecto "espremido".
    uint8_t *plane2 = (uint8_t *)0xA0000;
    for (int ch = 0; ch < 256; ch++) {
        uint8_t *slot = plane2 + ch * 32;
        const uint8_t *glyph = (ch >= 32 && ch <= 126)
            ? &font8x8_data[(ch - 32) * 8]
            : 0;
        for (int row = 0; row < 32; row++) {
            // row/2 → linha do glifo (0-7 dobrado para 0-15, resto zero)
            int grow = row / 2;
            if (glyph && grow < 8) {
                // Reverte ordem dos bits: LSB→MSB para VGA texto
                uint8_t b = glyph[grow];
                b = (uint8_t)(((b & 0xF0u) >> 4) | ((b & 0x0Fu) << 4));
                b = (uint8_t)(((b & 0xCCu) >> 2) | ((b & 0x33u) << 2));
                b = (uint8_t)(((b & 0xAAu) >> 1) | ((b & 0x55u) << 1));
                slot[row] = b;
            } else {
                slot[row] = 0x00;
            }
        }
    }

    // 4. Restaura sequencer e GC para modo texto normal
    outb(0x3C4, 0x00); outb(0x3C5, 0x01); // reset síncrono
    outb(0x3C4, 0x02); outb(0x3C5, 0x03); // map mask: planos 0+1
    outb(0x3C4, 0x04); outb(0x3C5, 0x02); // memory mode: odd/even
    outb(0x3C4, 0x00); outb(0x3C5, 0x03); // fim de reset

    outb(0x3CE, 0x04); outb(0x3CF, 0x00); // read map: plano 0
    outb(0x3CE, 0x05); outb(0x3CF, 0x10); // GC mode: odd/even write
    outb(0x3CE, 0x06); outb(0x3CF, 0x0E); // misc: 0xB8000, odd/even
}

// ----------------------------------------------------------------
// Modo 12h — 640×480, 16 cores
// (framebuffer planar em 0xA0000, 4 bitplanes, 80 bytes/linha)
// ----------------------------------------------------------------
void vga_set_mode12h(void) {
    // Miscellaneous Output: clock 25 MHz, sincs positivos, CRTC em 0x3D4
    outb(0x3C2, 0xE3);

    // Sequencer
    outb(0x3C4, 0x00); outb(0x3C5, 0x01); // reset assíncrono
    outb(0x3C4, 0x01); outb(0x3C5, 0x01); // clocking mode: 8 dots/char
    outb(0x3C4, 0x02); outb(0x3C5, 0x0F); // map mask: todos os planos
    outb(0x3C4, 0x03); outb(0x3C5, 0x00); // char map select
    outb(0x3C4, 0x04); outb(0x3C5, 0x06); // memory mode: sem chain-4 / sem odd-even
    outb(0x3C4, 0x00); outb(0x3C5, 0x03); // fim de reset

    // CRTC
    crtc_unlock();
    static const uint8_t crtc[] = {
        0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0x0B, 0x3E, // 0x00-0x07
        0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x08-0x0F
        0xEA, 0x8C, 0xDF, 0x28, 0x00, 0xE7, 0x04, 0xE3, // 0x10-0x17
        0xFF                                              // 0x18
    };
    for (int i = 0; i < (int)(sizeof crtc); i++) {
        outb(0x3D4, (uint8_t)i);
        outb(0x3D5, crtc[i]);
    }

    // Graphics Controller
    static const uint8_t gc[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0F, 0xFF
    };
    for (int i = 0; i < (int)(sizeof gc); i++) {
        outb(0x3CE, (uint8_t)i);
        outb(0x3CF, gc[i]);
    }

    // Attribute Controller (paleta 0-15 + mode control gráfico)
    static const uint8_t ac[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
        0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
        0x01, // índice 0x10: mode control (graphics, 16 cores)
        0x00, // índice 0x11: overscan
        0x0F, // índice 0x12: color plane enable
        0x00, // índice 0x13: h panning
        0x00  // índice 0x14: color select
    };
    write_ac(ac, sizeof ac);
}
