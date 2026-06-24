#include "bmp.h"
#include "shell.h"    // vfs_read
#include "vga_mode.h"
#include "io.h"
#include "keyboard.h"
#include <stdint.h>

// ---- Leitura little-endian sem alinhamento ----
static uint16_t r16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}
static uint32_t r32(const uint8_t *p) {
    return (uint32_t)(p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24));
}
static int32_t r32s(const uint8_t *p) {
    return (int32_t)r32(p);
}

// ---- Paleta modo 13h: cubo 6×6×6 (216 cores) ----
static void set_cube_palette(void) {
    for (int r = 0; r < 6; r++)
    for (int g = 0; g < 6; g++)
    for (int b = 0; b < 6; b++) {
        outb(0x3C8, (uint8_t)(r*36 + g*6 + b));
        outb(0x3C9, (uint8_t)(r * 63 / 5));
        outb(0x3C9, (uint8_t)(g * 63 / 5));
        outb(0x3C9, (uint8_t)(b * 63 / 5));
    }
    // Cor 216 = branco (para texto de status)
    outb(0x3C8, 216);
    outb(0x3C9, 63); outb(0x3C9, 63); outb(0x3C9, 63);
    // Cor 217 = preto
    outb(0x3C8, 217);
    outb(0x3C9, 0); outb(0x3C9, 0); outb(0x3C9, 0);
}

static void set_bmp_palette(const uint8_t *pal, int n) {
    for (int i = 0; i < n && i < 256; i++) {
        outb(0x3C8, (uint8_t)i);
        outb(0x3C9, pal[i*4+2] >> 2);  // R (BMP = BGR)
        outb(0x3C9, pal[i*4+1] >> 2);  // G
        outb(0x3C9, pal[i*4+0] >> 2);  // B
    }
}

static uint8_t map_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint8_t)((r * 5 / 255) * 36 + (g * 5 / 255) * 6 + (b * 5 / 255));
}

// ---- Desenha string simples em modo 13h ----
// (apenas para mensagem de erro)
static void m13_puts(int x, int y, const char *s) {
    // Usa fonte 8×8 do modo de texto — não disponível em modo 13h facilmente
    // Apenas colore um retângulo como indicador
    volatile uint8_t *vram = (volatile uint8_t *)0xA0000;
    int len = 0; while (s[len]) len++;
    for (int i = 0; i < len*8 && x+i < 320; i++)
        vram[y * 320 + x + i] = 216;
}

// ---- Aguarda tecla ou clique ----
static void wait_input(void) {
    while (1) {
        if (getchar_nonblock() > 0) break;
        asm volatile ("hlt");
    }
}

void bmp_view(const char *name, const char *ext) {
    const uint8_t *data = (const uint8_t *)vfs_read(name, ext);
    if (!data) return;

    // Valida assinatura BMP
    if (data[0] != 'B' || data[1] != 'M') return;

    uint32_t data_off  = r32 (data + 10);
    int32_t  width     = r32s(data + 18);
    int32_t  height    = r32s(data + 22);
    uint16_t bpp       = r16 (data + 28);
    uint32_t compr     = r32 (data + 30);
    int      flip      = (height > 0);    // positivo = bottom-up
    if (height < 0) height = -height;
    if (width  < 1 || width  > 16384) return;
    if (height < 1 || height > 16384) return;
    if (compr != 0) return;               // comprimido não suportado
    if (bpp != 24 && bpp != 8 && bpp != 4) return;

    uint32_t row_stride = ((width * bpp + 31) / 32) * 4;

    // Entra no modo 13h
    vga_set_mode13h();
    volatile uint8_t *vram = (volatile uint8_t *)0xA0000;

    // Limpa com preto
    for (int i = 0; i < 320*200; i++) vram[i] = 0;

    if (bpp == 24) {
        set_cube_palette();
        for (int sy = 0; sy < 200; sy++) {
            int src_y = (int)((int32_t)sy * height / 200);
            if (flip) src_y = height - 1 - src_y;
            const uint8_t *row = data + data_off + (uint32_t)src_y * row_stride;
            for (int sx = 0; sx < 320; sx++) {
                int src_x = (int)((int32_t)sx * width / 320);
                const uint8_t *px = row + src_x * 3;
                vram[sy * 320 + sx] = map_rgb(px[2], px[1], px[0]); // BGR→RGB
            }
        }
    } else if (bpp == 8) {
        uint32_t pal_off = r32(data + 14) + 14;
        int pal_n = (data_off - pal_off) / 4;
        if (pal_n < 1 || pal_n > 256) pal_n = 256;
        set_bmp_palette(data + pal_off, pal_n);
        for (int sy = 0; sy < 200; sy++) {
            int src_y = (int)((int32_t)sy * height / 200);
            if (flip) src_y = height - 1 - src_y;
            const uint8_t *row = data + data_off + (uint32_t)src_y * row_stride;
            for (int sx = 0; sx < 320; sx++) {
                int src_x = (int)((int32_t)sx * width / 320);
                vram[sy * 320 + sx] = row[src_x];
            }
        }
    } else {  // 4bpp (16 cores)
        uint32_t pal_off = r32(data + 14) + 14;
        int pal_n = (data_off - pal_off) / 4;
        if (pal_n < 1 || pal_n > 16) pal_n = 16;
        set_bmp_palette(data + pal_off, pal_n);
        for (int sy = 0; sy < 200; sy++) {
            int src_y = (int)((int32_t)sy * height / 200);
            if (flip) src_y = height - 1 - src_y;
            const uint8_t *row = data + data_off + (uint32_t)src_y * row_stride;
            for (int sx = 0; sx < 320; sx++) {
                int src_x = (int)((int32_t)sx * width / 320);
                uint8_t byte = row[src_x / 2];
                uint8_t idx  = (src_x & 1) ? (byte & 0x0F) : (byte >> 4);
                vram[sy * 320 + sx] = idx;
            }
        }
    }

    // Barra de status no rodapé
    for (int i = 0; i < 320; i++) vram[191 * 320 + i] = 217;
    for (int i = 0; i < 320; i++) vram[192 * 320 + i] = 217;
    m13_puts(4, 193, "Pressione qualquer tecla para voltar");

    wait_input();

    // Volta para modo 12h
    vga_set_mode12h();
}
