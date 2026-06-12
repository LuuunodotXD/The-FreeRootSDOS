#ifndef BMP_H
#define BMP_H
#include <stdint.h>

// Exibe BMP em tela cheia (modo 13h). Retorna ao modo 12h ao pressionar tecla.
// Suporta 24bpp e 8bpp não comprimido.
void bmp_view(const char *name, const char *ext);

#endif
