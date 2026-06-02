// vga12h.h — primitivas de desenho em modo 12h (640×480, 16 cores)
// Modo planar: 4 bitplanes, 1 bit por pixel por plano.
// Byte em 0xA0000 + y*80 + x/8; bit (7 - x%8) = pixel x no plano.
#ifndef VGA12H_H
#define VGA12H_H

#include <stdint.h>

#define VGA12_W  640
#define VGA12_H  480

// Primeiras 16 cores da paleta VGA padrão (compatíveis com modo 13h)
#define COL_BLACK    0
#define COL_BLUE     1
#define COL_GREEN    2
#define COL_CYAN     3
#define COL_RED      4
#define COL_MAGENTA  5
#define COL_BROWN    6
#define COL_LGRAY    7
#define COL_DGRAY    8
#define COL_LBLUE    9
#define COL_LGREEN  10
#define COL_LCYAN   11
#define COL_LRED    12
#define COL_WHITE   15

void vga12h_pixel (int x, int y, uint8_t c);
void vga12h_clear (uint8_t c);
void vga12h_hline (int x, int y, int len, uint8_t c);
void vga12h_vline (int x, int y, int len, uint8_t c);
void vga12h_rect  (int x, int y, int w, int h, uint8_t c);
void vga12h_border(int x, int y, int w, int h, uint8_t c);
void vga12h_char  (int x, int y, char ch, uint8_t fg, uint8_t bg);
void vga12h_string(int x, int y, const char *s, uint8_t fg, uint8_t bg);
int  vga12h_strpx (const char *s);

// Salva e restaura retângulo (para cursor do mouse)
void vga12h_save   (int x, int y, int w, int h, uint8_t *buf);
void vga12h_restore(int x, int y, int w, int h, const uint8_t *buf);

#endif
