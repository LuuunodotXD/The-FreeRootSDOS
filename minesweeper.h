#ifndef MINESWEEPER_H
#define MINESWEEPER_H
#include <stdint.h>

#define MS_COLS   9
#define MS_ROWS   9
#define MS_MINES  10
#define MS_CELL   16   // px por célula
#define HEADER_H   28

void ms_init(void);
void ms_draw(int x, int y, int w, int h);
void ms_click(int sx, int sy, int cx, int cy, int cw, int ch, int right);
// sx,sy = coordenadas na tela; cx,cy = origem do conteúdo da janela
// right: 0=esquerdo, 1=direito

#endif
