// balloon.h — interface gráfica Balloon (estilo Macintosh, modo 12h)
#ifndef BALLOON_H
#define BALLOON_H

#include <stdint.h>

// Máximo de janelas abertas ao mesmo tempo
#define BALLOON_MAX_WINDOWS 6

// Handle de janela (retornado por balloon_open)
typedef int BalloonWin;
#define BALLOON_INVALID -1

// Callback de conteúdo: recebe a área interna (sem borda/título)
typedef void (*BalloonDraw)(int x, int y, int w, int h);

// ---- API -------------------------------------------------------

void       balloon_init(void);          // inicializa e desenha desktop
BalloonWin balloon_open(const char *title,
                        int x, int y, int w, int h,
                        BalloonDraw draw_fn);
void       balloon_close(BalloonWin id);
void       balloon_redraw(void);        // redesenha tudo (desktop + janelas + cursor)
void       balloon_run(void);           // loop de eventos; retorna quando todas as janelas são fechadas

#endif
