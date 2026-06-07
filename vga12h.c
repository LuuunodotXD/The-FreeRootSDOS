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
// ----------------------------------------------------------------
// Retângulo cheio — otimizado: gc_solid/gc_restore chamados UMA vez,
// não por linha. Para rects alinhados a byte (caso mais comum no Balloon)
// a máscara de bit também é configurada uma só vez.
// ----------------------------------------------------------------
void vga12h_rect(int x, int y, int w, int h, uint8_t c) {
    if (w <= 0 || h <= 0) return;
    if (y < 0)           { h += y; y = 0; }
    if (y + h > VGA12_H) { h = VGA12_H - y; }
    if (x < 0)           { w += x; x = 0; }
    if (x + w > VGA12_W) { w = VGA12_W - x; }
    if (w <= 0 || h <= 0) return;

    int x1    = x + w - 1;
    int b0    = x  >> 3;
    int b1    = x1 >> 3;
    int lbits = x  & 7;
    int rbits = (x1 + 1) & 7; // 0 = borda direita alinhada

    gc_solid(c);

    if (lbits == 0 && rbits == 0) {
        // === Caso ideal: alinhado a byte dos dois lados ===
        // Uma única configuração de máscara fora do loop de linhas.
        outb(0x3CE, 0x08); outb(0x3CF, 0xFF);
        for (int row = y; row < y + h; row++) {
            volatile uint8_t *base = VRAM + (uint32_t)row * BYTES_PER_ROW;
            for (int b = b0; b <= b1; b++) base[b] = 0xFF;
        }
    } else if (b0 == b1) {
        // === Byte único por linha (largura ≤ 8 px) ===
        uint8_t mask = (uint8_t)((0xFF >> lbits) &
                                  (rbits ? (0xFF << (8 - rbits)) : 0xFF));
        outb(0x3CE, 0x08); outb(0x3CF, mask);
        for (int row = y; row < y + h; row++) {
            volatile uint8_t *p = VRAM + (uint32_t)row * BYTES_PER_ROW + b0;
            (void)*p; *p = 0xFF;
        }
    } else {
        // === Caso geral: bytes parciais nas bordas + bytes inteiros no meio ===
        int full0 = lbits ? (b0 + 1) : b0;
        int full1 = rbits ?  b1      : (b1 + 1); // exclusivo
        uint8_t ml = lbits ? (uint8_t)(0xFF >> lbits)       : 0;
        uint8_t mr = rbits ? (uint8_t)(0xFF << (8 - rbits)) : 0;

        for (int row = y; row < y + h; row++) {
            volatile uint8_t *base = VRAM + (uint32_t)row * BYTES_PER_ROW;
            if (lbits) {
                outb(0x3CE, 0x08); outb(0x3CF, ml);
                volatile uint8_t *p = base + b0; (void)*p; *p = 0xFF;
            }
            if (full0 < full1) {
                outb(0x3CE, 0x08); outb(0x3CF, 0xFF);
                for (int b = full0; b < full1; b++) base[b] = 0xFF;
            }
            if (rbits) {
                outb(0x3CE, 0x08); outb(0x3CF, mr);
                volatile uint8_t *p = base + b1; (void)*p; *p = 0xFF;
            }
        }
    }

    gc_restore();
}

void vga12h_border(int x, int y, int w, int h, uint8_t c) {
    vga12h_hline(x,         y,         w, c);
    vga12h_hline(x,         y + h - 1, w, c);
    vga12h_vline(x,         y,         h, c);
    vga12h_vline(x + w - 1, y,         h, c);
}

// Versão interna: não faz setup/restore do GC.
// Chamada por vga12h_string, que faz o setup UMA vez para toda a string.
static void vga12h_char_raw(int x, int y, char ch, uint8_t fg, uint8_t bg) {
    int idx = (unsigned char)ch;
    if (idx < 32 || idx > 126) idx = 32;
    const uint8_t *g = &font8x8_data[(idx - 32) * 8];
    int shift = x & 7;

    for (int row = 0; row < 8; row++) {
        int py = y + row;
        if ((unsigned)py >= VGA12_H) continue;
        uint8_t vga_fg = bitrev8(g[row]);
        uint8_t vga_bg = (uint8_t)(~vga_fg);
        volatile uint8_t *p0 = VRAM + (uint32_t)py * BYTES_PER_ROW + (x >> 3);

        if (shift == 0) {
            outb(0x3CE, 0x00); outb(0x3CF, fg);
            outb(0x3CE, 0x08); outb(0x3CF, vga_fg);
            (void)*p0; *p0 = 0xFF;
            outb(0x3CE, 0x00); outb(0x3CF, bg);
            outb(0x3CE, 0x08); outb(0x3CF, vga_bg);
            (void)*p0; *p0 = 0xFF;
        } else {
            uint8_t fg0 = (uint8_t)(vga_fg >> shift);
            uint8_t fg1 = (uint8_t)(vga_fg << (8 - shift));
            uint8_t char_mask0 = (uint8_t)(0xFF >> shift);
            uint8_t char_mask1 = (uint8_t)(0xFF << (8 - shift));
            uint8_t bg0 = (uint8_t)(char_mask0 & ~fg0);
            uint8_t bg1 = (uint8_t)(char_mask1 & ~fg1);
            volatile uint8_t *p1 = p0 + 1;
            outb(0x3CE, 0x00); outb(0x3CF, fg);
            if (fg0) { outb(0x3CE, 0x08); outb(0x3CF, fg0); (void)*p0; *p0 = 0xFF; }
            if (fg1) { outb(0x3CE, 0x08); outb(0x3CF, fg1); (void)*p1; *p1 = 0xFF; }
            outb(0x3CE, 0x00); outb(0x3CF, bg);
            if (bg0) { outb(0x3CE, 0x08); outb(0x3CF, bg0); (void)*p0; *p0 = 0xFF; }
            if (bg1) { outb(0x3CE, 0x08); outb(0x3CF, bg1); (void)*p1; *p1 = 0xFF; }
        }
    }
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

    // Enable Set/Reset UMA VEZ para o caractere inteiro.
    // gc_solid() fazia isso em todo laço de linha (2× por linha = 32 outb desperdiçados).
    outb(0x3CE, 0x01); outb(0x3CF, 0x0F);

    for (int row = 0; row < 8; row++) {
        int py = y + row;
        if ((unsigned)py >= VGA12_H) continue;

        uint8_t vga_fg = bitrev8(g[row]);
        uint8_t vga_bg = (uint8_t)(~vga_fg);

        volatile uint8_t *p0 = VRAM + (uint32_t)py * BYTES_PER_ROW + (x >> 3);

        if (shift == 0) {
            // Alinhado: 1 byte por linha — apenas 2 outb por fase fg/bg
            outb(0x3CE, 0x00); outb(0x3CF, fg);
            outb(0x3CE, 0x08); outb(0x3CF, vga_fg);
            (void)*p0; *p0 = 0xFF;

            outb(0x3CE, 0x00); outb(0x3CF, bg);
            outb(0x3CE, 0x08); outb(0x3CF, vga_bg);
            (void)*p0; *p0 = 0xFF;
        } else {
            // Desalinhado: glifo cruza dois bytes
            uint8_t fg0 = (uint8_t)(vga_fg >> shift);
            uint8_t fg1 = (uint8_t)(vga_fg << (8 - shift));
            uint8_t char_mask0 = (uint8_t)(0xFF >> shift);
            uint8_t char_mask1 = (uint8_t)(0xFF << (8 - shift));
            uint8_t bg0 = (uint8_t)(char_mask0 & ~fg0);
            uint8_t bg1 = (uint8_t)(char_mask1 & ~fg1);
            volatile uint8_t *p1 = p0 + 1;

            outb(0x3CE, 0x00); outb(0x3CF, fg);
            if (fg0) { outb(0x3CE, 0x08); outb(0x3CF, fg0); (void)*p0; *p0 = 0xFF; }
            if (fg1) { outb(0x3CE, 0x08); outb(0x3CF, fg1); (void)*p1; *p1 = 0xFF; }

            outb(0x3CE, 0x00); outb(0x3CF, bg);
            if (bg0) { outb(0x3CE, 0x08); outb(0x3CF, bg0); (void)*p0; *p0 = 0xFF; }
            if (bg1) { outb(0x3CE, 0x08); outb(0x3CF, bg1); (void)*p1; *p1 = 0xFF; }
        }
    }

    // Restaura GC ao estado passivo
    outb(0x3CE, 0x01); outb(0x3CF, 0x00);
    outb(0x3CE, 0x08); outb(0x3CF, 0xFF);
}

// ----------------------------------------------------------------
// String e medição
// ----------------------------------------------------------------
void vga12h_string(int x, int y, const char *s, uint8_t fg, uint8_t bg) {
    if (!*s) return;
    outb(0x3CE, 0x01); outb(0x3CF, 0x0F);   // Enable Set/Reset — uma vez
    while (*s) {
        vga12h_char_raw(x, y, *s++, fg, bg);
        x += 8;
    }
    outb(0x3CE, 0x01); outb(0x3CF, 0x00);   // restaura
    outb(0x3CE, 0x08); outb(0x3CF, 0xFF);
}

int vga12h_strpx(const char *s) { int n = 0; while 
    (*s++) n += 8; return n;
}

// ---------------------------------------------------------------- 
// Salva e restaura retângulo (usados pelo cursor 
// do mouse).//
// Save: lê plano por plano para reconstruir a cor 4-bit de cada pixel.
// Restore: reescreve pixel a pixel (velocidade aceitável para cursor pequeno).
// ----------------------------------------------------------------
// ----------------------------------------------------------------
// Salva e restaura retângulo (usados pelo cursor do mouse).
//
// Formato do buffer: 4 planos × BPR bytes × h linhas
//   onde BPR = ((w-1)/8) + 2  (bytes por linha, cobrindo qualquer alinhamento de x)
//
// Save: 1 outb de seleção de plano por plano → lê bytes inteiros da VRAM.
//   Era: (w × h × 4 planos × 2 outb) ≈ 3.700 outb para cursor 18×26.
//   Agora: apenas 4 × 2 = 8 outb para o cursor inteiro.
//
// Restore: Map Mask seleciona plano, write mode 0 grava bytes direto.
//   Mesma redução de outb.
//
// O buffer deve ter pelo menos VGA12H_SAVE_SIZE(w,h) bytes (definido em vga12h.h).
// ----------------------------------------------------------------
void vga12h_save(int x, int y, int w, int h, uint8_t *buf) {
    int b0  = x >> 3;                        // primeiro byte-coluna na VRAM
    int bpr = ((w - 1) >> 3) + 2;           // bytes por linha (cobre qualquer shift)
    for (int plane = 0; plane < 4; plane++) {
        outb(0x3CE, 0x04); outb(0x3CF, (uint8_t)plane); // Read Map Select
        for (int row = 0; row < h; row++) {
            int py = y + row;
            volatile uint8_t *src = VRAM + (uint32_t)py * BYTES_PER_ROW + b0;
            for (int b = 0; b < bpr; b++)
                *buf++ = ((unsigned)py < VGA12_H && b0 + b < BYTES_PER_ROW)
                          ? src[b] : 0;
        }
    }
    outb(0x3CE, 0x04); outb(0x3CF, 0x00); // restaura Read Map para plano 0
}

void vga12h_restore(int x, int y, int w, int h, const uint8_t *buf) {
    int b0  = x >> 3;
    int bpr = ((w - 1) >> 3) + 2;
    // Write mode 0 direto: desabilita Set/Reset, bit mask = tudo
    outb(0x3CE, 0x01); outb(0x3CF, 0x00);
    outb(0x3CE, 0x08); outb(0x3CF, 0xFF);
    for (int plane = 0; plane < 4; plane++) {
        outb(0x3C4, 0x02); outb(0x3C5, (uint8_t)(1 << plane)); // Map Mask = plano N
        for (int row = 0; row < h; row++) {
            int py = y + row;
            volatile uint8_t *dst = VRAM + (uint32_t)py * BYTES_PER_ROW + b0;
            for (int b = 0; b < bpr; b++) {
                if ((unsigned)py < VGA12_H && b0 + b < BYTES_PER_ROW) dst[b] = *buf;
                buf++;
            }
        }
    }
    outb(0x3C4, 0x02); outb(0x3C5, 0x0F); // Map Mask = todos os planos (estado normal)
}
