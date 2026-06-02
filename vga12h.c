// vga12h.c — primitivas de desenho em modo 12h (640×480, 16 cores)
//
// Modo planar VGA:
//   - VRAM em 0xA0000; largura = 80 bytes/linha (640 pixels / 8 bits)
//   - Pixel (x,y): byte = 0xA0000 + y*80 + x/8; bit = 7 - (x%8)
//   - Escrita usa Set/Reset + Enable Set/Reset para preencher todos os planos
//     com a cor desejada, controlando quais bits são afetados via Bit Mask.
//   - Latch read antes de cada write é obrigatório para bytes parciais (bit
//     mask != 0xFF) porque os bits não selecionados vêm do latch.
//   - Para bytes inteiros (bit mask 0xFF + enable set/reset 0x0F) o latch
//     não interfere; o read é omitido nesse caso para melhor performance.

#include "vga12h.h"
#include "font8x8.h"
#include "io.h"
#include <stdint.h>

#define VRAM            ((volatile uint8_t *)0xA0000)
#define BYTES_PER_ROW   80   // 640 / 8

// ----------------------------------------------------------------
// Helpers de registradores GC
// ----------------------------------------------------------------

// Configura Set/Reset para cor sólida em todos os planos.
// Todos os writes subsequentes usam essa cor para os bits selecionados.
static inline void gc_solid(uint8_t c) {
    outb(0x3CE, 0x00); outb(0x3CF, c & 0x0F); // Set/Reset = cor
    outb(0x3CE, 0x01); outb(0x3CF, 0x0F);      // Enable Set/Reset: todos os planos
}

// Restaura GC ao estado passivo (sem Set/Reset, bit mask total).
static inline void gc_restore(void) {
    outb(0x3CE, 0x01); outb(0x3CF, 0x00); // desabilita Set/Reset
    outb(0x3CE, 0x08); outb(0x3CF, 0xFF); // Bit Mask = todos os bits
}

// Escreve um byte parcial na VRAM usando latch + máscara de bits.
// Requer gc_solid() configurado antes.
static inline void put_masked(volatile uint8_t *p, uint8_t mask) {
    outb(0x3CE, 0x08); outb(0x3CF, mask);
    (void)*p; // latch read — necessário para bits fora da máscara
    *p = 0xFF;
}

// Reverte os bits de um byte (bit k → bit 7-k).
// Necessário porque font8x8_data tem bit0=pixel esquerdo,
// mas VGA modo 12h usa bit7=pixel esquerdo.
static inline uint8_t bitrev8(uint8_t b) {
    b = (uint8_t)(((b & 0xF0u) >> 4) | ((b & 0x0Fu) << 4));
    b = (uint8_t)(((b & 0xCCu) >> 2) | ((b & 0x33u) << 2));
    b = (uint8_t)(((b & 0xAAu) >> 1) | ((b & 0x55u) << 1));
    return b;
}

// ----------------------------------------------------------------
// Pixel único
// ----------------------------------------------------------------
void vga12h_pixel(int x, int y, uint8_t c) {
    if ((unsigned)x >= VGA12_W || (unsigned)y >= VGA12_H) return;
    gc_solid(c);
    put_masked(VRAM + (uint32_t)y * BYTES_PER_ROW + (x >> 3),
               (uint8_t)(0x80 >> (x & 7)));
    gc_restore();
}

// ----------------------------------------------------------------
// Clear (preenche a tela inteira)
// ----------------------------------------------------------------
void vga12h_clear(uint8_t c) {
    gc_solid(c);
    // Bit mask 0xFF + Enable Set/Reset 0x0F → latch read desnecessário
    outb(0x3CE, 0x08); outb(0x3CF, 0xFF);
    volatile uint8_t *p = VRAM;
    for (uint32_t i = 0; i < (uint32_t)VGA12_H * BYTES_PER_ROW; i++)
        *p++ = 0xFF;
    gc_restore();
}

// ----------------------------------------------------------------
// Linha horizontal
// ----------------------------------------------------------------
void vga12h_hline(int x, int y, int len, uint8_t c) {
    if (len <= 0) return;
    if ((unsigned)y >= VGA12_H) return;
    if (x < 0) { len += x; x = 0; }
    if (x >= VGA12_W) return;
    if (x + len > VGA12_W) len = VGA12_W - x;
    if (len <= 0) return;

    int x1 = x + len - 1;
    int b0 = x  >> 3;
    int b1 = x1 >> 3;
    volatile uint8_t *row = VRAM + (uint32_t)y * BYTES_PER_ROW;

    gc_solid(c);

    if (b0 == b1) {
        // Span inteiro dentro de um único byte
        uint8_t mask = (uint8_t)((0xFF >> (x & 7)) & (0xFF << (7 - (x1 & 7))));
        put_masked(row + b0, mask);
    } else {
        // Byte parcial esquerdo (somente se x não está alinhado a 8)
        if (x & 7) {
            put_masked(row + b0, (uint8_t)(0xFF >> (x & 7)));
            b0++;
        }
        // Bytes inteiros — sem latch read (cobertura total)
        outb(0x3CE, 0x08); outb(0x3CF, 0xFF);
        for (int b = b0; b < b1; b++)
            row[b] = 0xFF;
        // Byte parcial direito
        put_masked(row + b1, (uint8_t)(0xFF << (7 - (x1 & 7))));
    }

    gc_restore();
}

// ----------------------------------------------------------------
// Linha vertical
// ----------------------------------------------------------------
void vga12h_vline(int x, int y, int len, uint8_t c) {
    if ((unsigned)x >= VGA12_W) return;
    if (y < 0) { len += y; y = 0; }
    if (y + len > VGA12_H) len = VGA12_H - y;
    if (len <= 0) return;

    gc_solid(c);
    uint8_t mask = (uint8_t)(0x80 >> (x & 7));
    outb(0x3CE, 0x08); outb(0x3CF, mask);
    volatile uint8_t *p = VRAM + (uint32_t)y * BYTES_PER_ROW + (x >> 3);
    for (int i = 0; i < len; i++, p += BYTES_PER_ROW) {
        (void)*p;
        *p = 0xFF;
    }
    gc_restore();
}

// ----------------------------------------------------------------
// Retângulo cheio / borda
// ----------------------------------------------------------------
void vga12h_rect(int x, int y, int w, int h, uint8_t c) {
    for (int row = y; row < y + h; row++)
        vga12h_hline(x, row, w, c);
}

void vga12h_border(int x, int y, int w, int h, uint8_t c) {
    vga12h_hline(x,         y,         w, c);
    vga12h_hline(x,         y + h - 1, w, c);
    vga12h_vline(x,         y,         h, c);
    vga12h_vline(x + w - 1, y,         h, c);
}

// ----------------------------------------------------------------
// Caractere 8×8
// Renderiza por linha: 2 writes por linha (fg + bg) no caso alinhado,
// 4 writes no caso desalinhado — muito mais eficiente que 64 pixel writes.
// ----------------------------------------------------------------
void vga12h_char(int x, int y, char ch, uint8_t fg, uint8_t bg) {
    int idx = (unsigned char)ch;
    if (idx < 32 || idx > 126) idx = 32;
    const uint8_t *g = &font8x8_data[(idx - 32) * 8];

    int shift = x & 7;

    for (int row = 0; row < 8; row++) {
        int py = y + row;
        if ((unsigned)py >= VGA12_H) continue;

        // font8x8_data: bit0 = pixel esquerdo; VGA modo 12h: bit7 = pixel esquerdo.
        // bitrev8 converte a ordem de bits.
        uint8_t vga_fg = bitrev8(g[row]);
        uint8_t vga_bg = (uint8_t)(~vga_fg);

        volatile uint8_t *p0 = VRAM + (uint32_t)py * BYTES_PER_ROW + (x >> 3);

        if (shift == 0) {
            // Alinhado: o glifo cabe exatamente em 1 byte
            gc_solid(fg);
            outb(0x3CE, 0x08); outb(0x3CF, vga_fg);
            (void)*p0; *p0 = 0xFF;

            gc_solid(bg);
            outb(0x3CE, 0x08); outb(0x3CF, vga_bg);
            (void)*p0; *p0 = 0xFF;
        } else {
            // Desalinhado: glifo cruza dois bytes consecutivos
            // Pixels do glifo nos dois bytes:
            //   byte esquerdo: máscara = 0xFF >> shift
            //   byte direito:  máscara = 0xFF << (8 - shift)
            uint8_t fg0 = (uint8_t)(vga_fg >> shift);
            uint8_t fg1 = (uint8_t)(vga_fg << (8 - shift));
            uint8_t char_mask0 = (uint8_t)(0xFF >> shift);
            uint8_t char_mask1 = (uint8_t)(0xFF << (8 - shift));
            uint8_t bg0 = (uint8_t)(char_mask0 & ~fg0);
            uint8_t bg1 = (uint8_t)(char_mask1 & ~fg1);
            volatile uint8_t *p1 = p0 + 1;

            gc_solid(fg);
            if (fg0) { outb(0x3CE, 0x08); outb(0x3CF, fg0); (void)*p0; *p0 = 0xFF; }
            if (fg1) { outb(0x3CE, 0x08); outb(0x3CF, fg1); (void)*p1; *p1 = 0xFF; }

            gc_solid(bg);
            if (bg0) { outb(0x3CE, 0x08); outb(0x3CF, bg0); (void)*p0; *p0 = 0xFF; }
            if (bg1) { outb(0x3CE, 0x08); outb(0x3CF, bg1); (void)*p1; *p1 = 0xFF; }
        }
    }
    gc_restore();
}

// ----------------------------------------------------------------
// String e medição
// ----------------------------------------------------------------
void vga12h_string(int x, int y, const char *s, uint8_t fg, uint8_t bg) {
    while (*s) {
        vga12h_char(x, y, *s++, fg, bg);
        x += 8;
    }
}

int vga12h_strpx(const char *s) {
    int n = 0;
    while (*s++) n += 8;
    return n;
}

// ----------------------------------------------------------------
// Salva e restaura retângulo (usados pelo cursor do mouse).
//
// Save: lê plano por plano para reconstruir a cor 4-bit de cada pixel.
// Restore: reescreve pixel a pixel (velocidade aceitável para cursor pequeno).
// ----------------------------------------------------------------
void vga12h_save(int x, int y, int w, int h, uint8_t *buf) {
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            int px = x + col, py = y + row;
            if ((unsigned)px >= VGA12_W || (unsigned)py >= VGA12_H) {
                *buf++ = 0;
                continue;
            }
            uint32_t offs   = (uint32_t)py * BYTES_PER_ROW + (px >> 3);
            uint8_t  bitpos = (uint8_t)(7 - (px & 7));
            uint8_t  color  = 0;
            for (int plane = 0; plane < 4; plane++) {
                outb(0x3CE, 0x04); outb(0x3CF, (uint8_t)plane); // Read Map Select
                if ((VRAM[offs] >> bitpos) & 1)
                    color |= (uint8_t)(1 << plane);
            }
            *buf++ = color;
        }
    }
    outb(0x3CE, 0x04); outb(0x3CF, 0x00); // restaura Read Map para plano 0
}

void vga12h_restore(int x, int y, int w, int h, const uint8_t *buf) {
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++)
            vga12h_pixel(x + col, y + row, *buf++);
}
