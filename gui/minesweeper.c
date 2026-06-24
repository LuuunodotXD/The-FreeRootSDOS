#include "minesweeper.h"
#include "vga12h.h"
#include "idt.h"
#include <stdint.h>

// ---- Paleta de números ----
static const uint8_t num_col[9] = {
    0, 1, 2, 12, 9, 4, 3, 0, 8  // 0=unused, 1=blue, 2=green, 3=lred...
};

// ---- Estado do jogo ----
#define MS_HIDDEN   0
#define MS_REVEALED 1
#define MS_FLAGGED  2

#define MS_IDLE    0
#define MS_PLAYING 1
#define MS_WON     2
#define MS_LOST    3

static uint8_t ms_cell[MS_ROWS][MS_COLS]; // bit7=mina, bits3:0=vizinhos
static uint8_t ms_vis [MS_ROWS][MS_COLS]; // HIDDEN/REVEALED/FLAGGED
static int     ms_game;
static int     ms_flags;
static int     ms_revealed;
static int     ms_first;  // 1 = ainda não houve primeiro clique
static uint32_t ms_t0;
static int     ms_lost_r, ms_lost_c;  // célula clicada na derrota

// ---- RNG simples ----
static uint32_t ms_seed;
static uint32_t ms_rand(void) {
    ms_seed = ms_seed * 1664525u + 1013904223u;
    return ms_seed;
}

void ms_init(void) {
    ms_seed = timer_get_ticks() + 1;
    for (int r=0;r<MS_ROWS;r++) for(int c=0;c<MS_COLS;c++) {
        ms_cell[r][c] = 0;
        ms_vis [r][c] = MS_HIDDEN;
    }
    ms_game     = MS_PLAYING;
    ms_flags    = 0;
    ms_revealed = 0;
    ms_first    = 1;
    ms_t0       = 0;
    ms_lost_r   = -1;
    ms_lost_c   = -1;
}

// ---- Coloca minas evitando (er,ec) ----
static void ms_place_mines(int er, int ec) {
    int placed = 0;
    while (placed < MS_MINES) {
        int r = (int)(ms_rand() % MS_ROWS);
        int c = (int)(ms_rand() % MS_COLS);
        if (ms_cell[r][c] & 0x80) continue;
        if (r==er && c==ec)       continue;
        ms_cell[r][c] |= 0x80;
        placed++;
    }
    // Calcula contagens
    for (int r=0;r<MS_ROWS;r++) for(int c=0;c<MS_COLS;c++) {
        if (ms_cell[r][c] & 0x80) continue;
        int cnt = 0;
        for (int dr=-1;dr<=1;dr++) for(int dc=-1;dc<=1;dc++) {
            int nr=r+dr, nc=c+dc;
            if (nr<0||nr>=MS_ROWS||nc<0||nc>=MS_COLS) continue;
            if (ms_cell[nr][nc] & 0x80) cnt++;
        }
        ms_cell[r][c] = (uint8_t)cnt;
    }
}

// ---- Reveal flood-fill iterativo ----
static void ms_reveal(int sr, int sc) {
    // Stack simples
    int8_t stk_r[MS_ROWS*MS_COLS], stk_c[MS_ROWS*MS_COLS];
    int top = 0;
    stk_r[top] = (int8_t)sr; stk_c[top] = (int8_t)sc; top++;

    while (top > 0) {
        top--;
        int r = stk_r[top], c = stk_c[top];
        if (r<0||r>=MS_ROWS||c<0||c>=MS_COLS) continue;
        if (ms_vis[r][c] == MS_REVEALED) continue;
        if (ms_vis[r][c] == MS_FLAGGED)  continue;
        ms_vis[r][c] = MS_REVEALED;
        ms_revealed++;
        if ((ms_cell[r][c] & 0x7F) == 0 && !(ms_cell[r][c] & 0x80)) {
            for (int dr=-1;dr<=1;dr++) for(int dc=-1;dc<=1;dc++) {
                if (dr==0&&dc==0) continue;
                int nr=r+dr,nc=c+dc;
                if (nr>=0&&nr<MS_ROWS&&nc>=0&&nc<MS_COLS)
                    if (ms_vis[nr][nc]==MS_HIDDEN && top<MS_ROWS*MS_COLS-1) {
                        stk_r[top]=(int8_t)nr; stk_c[top]=(int8_t)nc; top++;
                    }
            }
        }
    }
}

// ---- Desenho de células ----
#define HEADER_H 28

static void draw_cell(int cx, int cy, int r, int c) {
    uint8_t vis  = ms_vis[r][c];
    uint8_t cell = ms_cell[r][c];
    int x = cx + c * MS_CELL;
    int y = cy + r * MS_CELL;

    if (vis == MS_HIDDEN || vis == MS_FLAGGED) {
        // Célula coberta: efeito 3D
        vga12h_rect  (x, y, MS_CELL, MS_CELL, 7);
        vga12h_hline (x+1, y+1, MS_CELL-2, 15);  // branco topo
        vga12h_vline (x+1, y+1, MS_CELL-2, 15);  // branco esquerda
        vga12h_hline (x+1, y+MS_CELL-1, MS_CELL-1, 8); // cinza escuro baixo
        vga12h_vline (x+MS_CELL-1, y+1, MS_CELL-1, 8); // cinza escuro direita

        if (vis == MS_FLAGGED) {
            // Bandeira: haste vertical + triângulo vermelho
            vga12h_vline(x+7, y+4, 9, 0);
            vga12h_rect (x+4, y+4, 5, 3, 4);   // vermelho
            vga12h_rect (x+4, y+7, 5, 1, 0);   // pé
            vga12h_hline(x+4, y+12, 8, 0);     // base
        }
    } else {
        // Célula revelada
        vga12h_rect(x, y, MS_CELL, MS_CELL, ms_game==MS_LOST && (ms_cell[r][c]&0x80) ? 4 : 7);
        vga12h_border(x, y, MS_CELL, MS_CELL, 8);

        if (cell & 0x80) {
            // Mina: círculo preto com espinhos
            vga12h_rect (x+5, y+4, 6, 8, 0);
            vga12h_rect (x+4, y+5, 8, 6, 0);
            // Espinhos
            vga12h_pixel(x+5, y+3, 0); vga12h_pixel(x+10, y+3, 0);
            vga12h_pixel(x+3, y+5, 0); vga12h_pixel(x+12, y+5, 0);
            vga12h_pixel(x+3, y+10, 0); vga12h_pixel(x+12, y+10, 0);
            vga12h_pixel(x+5, y+12, 0); vga12h_pixel(x+10, y+12, 0);
            vga12h_pixel(x+8, y+6, 15); // brilho
        } else {
            int n = cell & 0x7F;
            if (n > 0) {
                char s[2] = { (char)('0' + n), 0 };
                vga12h_string(x+4, y+4, s, num_col[n], 7);
            }
        }
    }

    // Célula da derrota: fundo vermelho brilhante
    if (ms_game == MS_LOST && r==ms_lost_r && c==ms_lost_c) {
        vga12h_rect(x+1, y+1, MS_CELL-2, MS_CELL-2, 12);
        vga12h_rect (x+5, y+4, 6, 8, 0);
        vga12h_rect (x+4, y+5, 8, 6, 0);
    }
}

// ---- Cabeçalho: contador de minas, smile, tempo ----
static void draw_header(int x, int y, int w) {
    vga12h_rect(x, y, w, HEADER_H, 7);
    vga12h_border(x, y, w, HEADER_H, 8);

    // Contador de minas (esquerda)
    int left = MS_MINES - ms_flags;
    if (left < 0) left = 0;
    char cbuf[4];
    cbuf[0] = (char)('0' + (left/100)%10);
    cbuf[1] = (char)('0' + (left/10)%10);
    cbuf[2] = (char)('0' + left%10);
    cbuf[3] = 0;
    vga12h_rect(x+4, y+4, 26, 20, 0);
    vga12h_string(x+6, y+8, cbuf, 4, 0);  // vermelho sobre preto

    // Botão smile (centro)
    int sx = x + w/2 - 10;
    int sy = y + 4;
    vga12h_rect  (sx, sy, 20, 20, 14);   // amarelo
    vga12h_border(sx, sy, 20, 20, 8);
    if (ms_game == MS_PLAYING || ms_game == MS_IDLE) {
        // Olhos
        vga12h_pixel(sx+6,  sy+7, 0);
        vga12h_pixel(sx+13, sy+7, 0);
        // Sorriso
        vga12h_hline(sx+5, sy+13, 10, 0);
        vga12h_pixel(sx+4,  sy+12, 0);
        vga12h_pixel(sx+15, sy+12, 0);
    } else if (ms_game == MS_LOST) {
        // X nos olhos
        vga12h_pixel(sx+5,  sy+6, 0); vga12h_pixel(sx+7,  sy+8, 0);
        vga12h_pixel(sx+7,  sy+6, 0); vga12h_pixel(sx+5,  sy+8, 0);
        vga12h_pixel(sx+12, sy+6, 0); vga12h_pixel(sx+14, sy+8, 0);
        vga12h_pixel(sx+14, sy+6, 0); vga12h_pixel(sx+12, sy+8, 0);
        // Boca triste
        vga12h_hline(sx+5, sy+14, 10, 0);
        vga12h_pixel(sx+4, sy+15, 0);
        vga12h_pixel(sx+15,sy+15, 0);
    } else {
        // Óculos (vitória)
        vga12h_border(sx+4,  sy+6, 5, 5, 0);
        vga12h_border(sx+11, sy+6, 5, 5, 0);
        vga12h_hline(sx+5, sy+12, 10, 0);
        vga12h_pixel(sx+4, sy+11, 0);
        vga12h_pixel(sx+15,sy+11, 0);
    }

    // Timer (direita)
    uint32_t elapsed = (ms_game == MS_PLAYING && !ms_first)
                     ? (timer_get_ticks() - ms_t0) / 1000 : 0;
    if (elapsed > 999) elapsed = 999;
    char tbuf[4];
    tbuf[0] = (char)('0' + (elapsed/100)%10);
    tbuf[1] = (char)('0' + (elapsed/10)%10);
    tbuf[2] = (char)('0' + elapsed%10);
    tbuf[3] = 0;
    vga12h_rect(x+w-30, y+4, 26, 20, 0);
    vga12h_string(x+w-28, y+8, tbuf, 4, 0);
}

// ---- API pública ----
void ms_draw(int x, int y, int w, int h) {
    (void)h;
    vga12h_rect(x, y, w, MS_ROWS*MS_CELL + HEADER_H + 8, 7);
    draw_header(x+4, y+2, w-8);
    int gx = x + (w - MS_COLS*MS_CELL) / 2;
    int gy = y + HEADER_H + 4;
    vga12h_border(gx-1, gy-1, MS_COLS*MS_CELL+2, MS_ROWS*MS_CELL+2, 8);
    for (int r=0;r<MS_ROWS;r++)
        for (int c=0;c<MS_COLS;c++)
            draw_cell(gx, gy, r, c);
}

void ms_click(int sx, int sy, int cx, int cy, int cw, int ch, int right) {
    (void)ch; // altura da área não usada, mas mantida por compatibilidade

    int grid_w = MS_COLS * MS_CELL;
    int grid_h = MS_ROWS * MS_CELL;
    int gx = cx + (cw - grid_w) / 2;
    int gy = cy + HEADER_H + 4;

    // Verifica se o clique está dentro da grade
    if (sx < gx || sx >= gx + grid_w || sy < gy || sy >= gy + grid_h)
        return;

    int c = (sx - gx) / MS_CELL;
    int r = (sy - gy) / MS_CELL;

    // Clique no smile? (opcional: pode tratar aqui ou deixar para o evento de janela)
    // Por simplicidade, melhor ignorar smile por enquanto, pois já há tratamento no balloon?

    if (ms_game == MS_WON || ms_game == MS_LOST) return;

    if (right) {
        if (ms_vis[r][c] == MS_HIDDEN) {
            ms_vis[r][c] = MS_FLAGGED;
            ms_flags++;
        } else if (ms_vis[r][c] == MS_FLAGGED) {
            ms_vis[r][c] = MS_HIDDEN;
            ms_flags--;
        }
        return;
    }

    // Clique esquerdo
    if (ms_vis[r][c] == MS_FLAGGED || ms_vis[r][c] == MS_REVEALED)
        return;

    if (ms_first) {
        ms_place_mines(r, c);
        ms_first = 0;
        ms_t0 = timer_get_ticks();
    }

    if (ms_cell[r][c] & 0x80) {
        ms_lost_r = r; ms_lost_c = c;
        ms_game = MS_LOST;
        for (int i = 0; i < MS_ROWS; i++)
            for (int j = 0; j < MS_COLS; j++)
                if ((ms_cell[i][j] & 0x80) && ms_vis[i][j] != MS_FLAGGED)
                    ms_vis[i][j] = MS_REVEALED;
        return;
    }

    ms_reveal(r, c);

    if (ms_revealed == MS_ROWS * MS_COLS - MS_MINES)
        ms_game = MS_WON;
}
