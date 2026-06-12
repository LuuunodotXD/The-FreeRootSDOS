// balloon.c — interface gráfica Balloon
// Modo: VGA 12h — 640×480, 16 cores
// Funcionalidades: barra de menus com relógio 12h, ícones no desktop, janelas arrastáveis,
//                  terminal gráfico, explorador de arquivos, janela "Sobre"

#include "balloon.h"
#include "vga12h.h"
#include "mouse.h"
#include "keyboard.h"
#include "terminal.h"
#include "shell.h"
#include "idt.h"
#include "fs.h"
#include "fs_disk.h"
#include "io.h"
#include "adlib.h"
#include "rtl8139.h"
#include "net.h"
#include "arp.h"
#include "tcp.h"
#include "dns.h"
#include "bmp.h"
#include "minesweeper.h"
#include <stdint.h>
// ----------------------------------------------------------------
// Cores
// ----------------------------------------------------------------
#define C_BLACK   0
#define C_BLUE    1
#define C_GREEN   2
#define C_CYAN    3
#define C_RED     4
#define C_MAGENTA 5
#define C_BROWN   6
#define C_LGRAY   7
#define C_DGRAY   8
#define C_LBLUE   9
#define C_LGREEN 10
#define C_LCYAN  11
#define C_LRED   12
#define C_PINK   13
#define C_YELLOW 14
#define C_WHITE  15

// ----------------------------------------------------------------
// Layout
// ----------------------------------------------------------------
#define MENUBAR_H    14
#define TITLEBAR_H   13
#define CLOSEBOX_SZ  11
#define MINBOX_SZ    11          // ← linha nova
#define MINBOX_OFF   (CLOSEBOX_SZ + 3)   // offset da closebox
#define RESIZE_SZ    8
#define WIN_MIN_W    120
#define WIN_MIN_H    (TITLEBAR_H + 40)
#define MAXBOX_SZ    11
#define MAXBOX_OFF   (MINBOX_OFF + MINBOX_SZ + 3)

static const struct { int x; const char *label; } menu_items[] = {
    {   6, "Balloon" },
    {  94, "File"    },
    { 158, "Edit"    },
    {   0, 0         },
};

// Dropdown
#define DROPDOWN_X    4
#define DROPDOWN_W  148
#define DD_TOP        MENUBAR_H
#define DD_ABOUT_Y   (DD_TOP + 4)
#define DD_SEP_Y     (DD_TOP + 18)
#define DD_QUIT_Y    (DD_TOP + 22)
#define DD_BOTTOM    (DD_TOP + 36)
#define DROPDOWN_H   (DD_BOTTOM - DD_TOP)
#define DD_ABOUT_TOP (DD_TOP + 2)
#define DD_ABOUT_BOT (DD_TOP + 16)
#define DD_QUIT_TOP  (DD_TOP + 18)
#define DD_QUIT_BOT  (DD_BOTTOM - 2)

static int dropdown_open = 0;

// ----------------------------------------------------------------
// Janelas
// ----------------------------------------------------------------
typedef struct {
    int         active;
    int         minimized;
    int         maximized;
    int         saved_x, saved_y, saved_w, saved_h;
    int         x, y, w, h;
    char        title[32];
    BalloonDraw draw_fn;
} Window;

static Window windows[BALLOON_MAX_WINDOWS];
static int    num_windows = 0;

// ----------------------------------------------------------------
// Ícones do desktop
// ----------------------------------------------------------------
#define ICON_IMG_W    48
#define ICON_IMG_H    36
#define ICON_GAP_Y     6
#define ICON_TOTAL_H  (ICON_IMG_H + ICON_GAP_Y + 8)  // 50

#define ICON_START_X  20
#define ICON_START_Y  (MENUBAR_H + 18)
#define ICON_STRIDE   80
#define BR_MAX_LINES 180
#define BR_LINE_LEN   68
#define BR_LH         10
#define ICON_COUNT     7
#define ED_MAX_LINES 200
#define ED_LINE_LEN   68
#define ED_LH         10

// Forward declarations de funções usadas por draw_icon
static inline void icon_pos(int i, int *ox, int *oy);
static void draw_window(int i);

// ----------------------------------------------------------------
// RTC
// ----------------------------------------------------------------
static uint8_t rtc_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static uint8_t bcd2bin(uint8_t b) {
    return (uint8_t)(((b >> 4) * 10) + (b & 0x0F));
}

static void rtc_get_time(int *h, int *m, int *s) {
    while (rtc_read(0x0A) & 0x80) {}
    uint8_t sec = rtc_read(0x00);
    uint8_t min = rtc_read(0x02);
    uint8_t hr  = rtc_read(0x04);
    uint8_t sb  = rtc_read(0x0B);
    int is_binary = sb & 0x04;
    int is_24h    = sb & 0x02;
    if (!is_binary) {
        uint8_t pm_bit = hr & 0x80;
        hr  = bcd2bin(hr & 0x7F) | pm_bit;
        min = bcd2bin(min);
        sec = bcd2bin(sec);
    }
    if (!is_24h) {
        int pm = (hr & 0x80) != 0;
        hr &= 0x7F;
        if (hr == 12) hr = 0;
        if (pm)       hr = (uint8_t)(hr + 12);
    }
    *h = hr; *m = min; *s = sec;
}

static void fmt_time_12h(int h, int m, int s, char *buf) {
    int h12     = h % 12;
    if (h12 == 0) h12 = 12;
    const char *ap = (h >= 12) ? "PM" : "AM";
    buf[0]  = (h12 >= 10) ? '1' : ' ';
    buf[1]  = '0' + (h12 % 10);
    buf[2]  = ':';
    buf[3]  = '0' + m / 10;
    buf[4]  = '0' + m % 10;
    buf[5]  = ':';
    buf[6]  = '0' + s / 10;
    buf[7]  = '0' + s % 10;
    buf[8]  = ' ';
    buf[9]  = ap[0];
    buf[10] = ap[1];
    buf[11] = '\0';
}

// ----------------------------------------------------------------
// Cursor
// ----------------------------------------------------------------
#define CUR_LOG_W  9
#define CUR_LOG_H 13
#define CUR_SCALE  2
#define CUR_W  (CUR_LOG_W * CUR_SCALE)
#define CUR_H  (CUR_LOG_H * CUR_SCALE)
static const uint16_t cur_outline[CUR_LOG_H] = {
    0b110000000, 0b111000000, 0b111100000, 0b111110000,
    0b111111000, 0b111111100, 0b111111110, 0b111110000,
    0b111011000, 0b100001100, 0b000001100, 0b000000110,
    0b000000000,
};
static const uint16_t cur_black[CUR_LOG_H] = {
    0b100000000, 0b110000000, 0b111000000, 0b111100000,
    0b111110000, 0b111111000, 0b111111100, 0b111110000,
    0b101001000, 0b000000100, 0b000000100, 0b000000010,
    0b000000000,
};

static uint8_t cur_save[CUR_W * CUR_H];
static int     cur_sx = -1, cur_sy = -1;

static void cursor_erase(void) {
    if (cur_sx < 0) return;
    vga12h_restore(cur_sx, cur_sy, CUR_W, CUR_H, cur_save);
    cur_sx = cur_sy = -1;
}

static void cursor_draw(int x, int y) {
    vga12h_save(x, y, CUR_W, CUR_H, cur_save);
    cur_sx = x; cur_sy = y;
    for (int row = 0; row < CUR_LOG_H; row++) {
        for (int col = 0; col < CUR_LOG_W; col++) {
            uint16_t bit = (uint16_t)(0x100 >> col);
            uint8_t c;
            if      (cur_black[row]   & bit) c = C_BLACK;
            else if (cur_outline[row] & bit) c = C_WHITE;
            else continue;
            vga12h_pixel(x + col*2,     y + row*2,     c);
            vga12h_pixel(x + col*2 + 1, y + row*2,     c);
            vga12h_pixel(x + col*2,     y + row*2 + 1, c);
            vga12h_pixel(x + col*2 + 1, y + row*2 + 1, c);
        }
    }
}

// ----------------------------------------------------------------
// Conteúdo das janelas (declarações antecipadas)
// ----------------------------------------------------------------
static void draw_about_content  (int x, int y, int w, int h);
static void draw_gterm_content  (int x, int y, int w, int h);
static void draw_win_files      (int x, int y, int w, int h);
static void draw_net_content    (int x, int y, int w, int h);
static void draw_browser_content(int x, int y, int w, int h);
static void draw_editor_content (int x, int y, int w, int h);
static void draw_minesweeper(int x, int y, int w, int h) {
    ms_draw(x, y, w, h);
}

typedef struct {
    const char *label;
    const char *win_title;
    int         win_w, win_h;
    BalloonDraw draw_content;
} IconDef;

static const IconDef icon_defs[ICON_COUNT] = {
    { "Terminal", "Terminal",        610, 306, draw_gterm_content   },
    { "Arquivos", "Arquivos",        500, 300, draw_win_files       },
    { "Rede",     "Painel de Rede",  300, 180, draw_net_content     },
    { "Browser",  "Browser",         580, 340, draw_browser_content },
    { "Minas",    "Campo Minado", MS_COLS*MS_CELL+16, MS_ROWS*MS_CELL+HEADER_H+12, draw_minesweeper },
    { "Editor",   "Editor de Texto", 560, 340, draw_editor_content  },
    { "Sobre",    "Sobre o Balloon", 280, 120, draw_about_content   },
};

static int icon_selected    = -1;
static int icon_last_clicked = -1;

// ----------------------------------------------------------------
// Programas gerais (declarações antecipadas)
// ----------------------------------------------------------------
#define GTERM_COLS  74
#define GTERM_ROWS  26
#define GT_LH       10
#define GT_PAD       4
#define GT_PAD_V     3
#define GT_BG        0
#define GT_FG       10
#define GT_IN_FG    15
#define GT_PR_FG    14
#define GT_CUR_BG    7
#define GT_SEP_C     8
#define GT_HIST_MAX 10
#define CTX_MAX_ITEMS    8
#define CTX_ITEM_H      14
#define CTX_ITEM_W     160
#define CTX_BUF_SIZE     VGA12H_SAVE_SIZE(CTX_ITEM_W + 4, CTX_MAX_ITEMS * CTX_ITEM_H + 6)
#define CTX_OPEN_EDITOR  1
#define CTX_DELETE_FILE  2
#define CTX_OPEN_DIR     3
#define CTX_CLOSE_WIN    4
#define CTX_MIN_WIN      5
#define CTX_NEW_FILE     6
#define CTX_REFRESH      7
#define CTX_VIEW_IMAGE   8

typedef struct { char label[32]; int action; } CtxItem;

static int   ctx_open   = 0;
static int   ctx_x, ctx_y, ctx_w, ctx_h;
static CtxItem ctx_items[CTX_MAX_ITEMS];
static int   ctx_nitems = 0;
static int   ctx_hover  = -1;
static int   ctx_fexp_idx = -1;
static int   ctx_win_id   = -1;
static uint8_t ctx_bg_buf[CTX_BUF_SIZE];
static char  gt_hist[GT_HIST_MAX][GTERM_COLS + 1];
static int   gt_hist_count = 0;
static int   gt_hist_head  = 0;
static int   gt_hist_idx   = -1;
static char  gt_saved[GTERM_COLS + 1];   // linha salva antes de navegar
static char  gt_buf[GTERM_ROWS][GTERM_COLS + 1];
static int   gt_out_row;
static int   gt_out_col;
static char  gt_in[GTERM_COLS + 1];
static int   gt_in_len;
static int   gt_in_cur;
static int   gt_win_id = -1;
static int   gt_dirty_from = 0;
static int   drag_id    = -1;   // ← linha nova
static int   drag_ox    = 0;    // ← linha nova
static int   drag_oy    = 0;    // ← linha nova
static int   resize_id = -1;
static int   resize_ox = 0, resize_oy = 0;
static int   resize_left = 0;  // 1 = arrastando pelo canto esquerdo
static int   br_win_id = -1;
static char  br_url[128]  = {0};
static int   br_url_len   = 0;
static char  br_lines[BR_MAX_LINES][BR_LINE_LEN + 1];
static int   br_nlines    = 0;
static int   br_scroll    = 0;
static uint8_t br_fetch_buf[32768];
static int   ed_win_id   = -1;
static char  ed_lines[ED_MAX_LINES][ED_LINE_LEN + 1];
static int   ed_nlines;
static int   ed_row, ed_col, ed_scroll;
static int   ed_modified;
static char  ed_name[9];   // base do filename
static char  ed_ext[4];    // extensão
static int   ed_ask_save;  // mostrando prompt de salvar
static char  ed_ask_buf[12];
static int   ed_ask_len;
static int   ms_win_id = -1;
// Buffer de save (~14KB em BSS)
static char  ed_save_buf[ED_MAX_LINES * (ED_LINE_LEN + 2)];

// Sobe todas as linhas uma posição e limpa a última
static void gterm_scroll(void) {
    for (int r = 0; r < GTERM_ROWS - 1; r++) {
        int c = 0;
        while ((gt_buf[r][c] = gt_buf[r + 1][c])) c++;
    }
    gt_buf[GTERM_ROWS - 1][0] = 0;
    gt_dirty_from = 0;  // tudo mudou
}

static void gterm_hook(char c);
static void gterm_open(int win_id);
static void gterm_key(int key);
static void gterm_redraw_only(void);
static void draw_gterm_content(int x, int y, int w, int h);

static void draw_minesweeper(int x, int y, int w, int h);
// ----------------------------------------------------------------
// Explorador de arquivos (declarações antecipadas)
// ----------------------------------------------------------------
typedef struct {
    int        active;
    char       drive;
    uint8_t    dir_idx;
    int        entry_count;
    char       names[64][9];
    char       ext[64][4];
    int        is_dir[64];
    int        selected;
    int        scroll;
} FileExpState;

static FileExpState fexp;
static int fexp_win_id  = -1;

static void fexp_load_entries(void);
static void fexp_open_dir(int idx);
static void fexp_init_win(void);
static void draw_win_files(int x, int y, int w, int h);

// ----------------------------------------------------------------
// Imagens dos ícones
// ----------------------------------------------------------------
static void draw_icon_img_terminal(int x, int y) {
    vga12h_rect  (x,      y,      ICON_IMG_W,     ICON_IMG_H - 6, C_DGRAY);
    vga12h_border(x,      y,      ICON_IMG_W,     ICON_IMG_H - 6, C_BLACK);
    vga12h_rect  (x + 3,  y + 3,  ICON_IMG_W - 6, ICON_IMG_H - 14, C_BLACK);
    vga12h_string(x + 6,  y + 8,  ">_",  C_LGREEN, C_BLACK);
    vga12h_rect  (x + 16, y + ICON_IMG_H - 6, 16, 4, C_DGRAY);
    vga12h_hline (x + 10, y + ICON_IMG_H - 2, 28,    C_DGRAY);
    vga12h_hline (x + 10, y + ICON_IMG_H - 1, 28,    C_BLACK);
}

static void draw_icon_img_files(int x, int y) {
    int fold = 10;
    int dw = 36, dh = 34;
    int dx = x + 6, dy = y + 1;
    vga12h_rect(dx, dy, dw, dh, C_WHITE);
    vga12h_rect(dx + dw - fold, dy, fold, fold, C_LGRAY);
    vga12h_vline(dx,              dy,          dh,         C_BLACK);
    vga12h_hline(dx,              dy + dh - 1, dw,         C_BLACK);
    vga12h_hline(dx,              dy,          dw - fold,  C_BLACK);
    vga12h_vline(dx + dw - 1,     dy + fold,   dh - fold,  C_BLACK);
    for (int fi = 0; fi < fold; fi++)
        vga12h_pixel(dx + dw - fold + fi, dy + fold - 1 - fi, C_BLACK);
    vga12h_hline(dx + dw - fold, dy + fold, fold, C_BLACK);
    vga12h_hline(dx + 4, dy + 14, dw - 16, C_DGRAY);
    vga12h_hline(dx + 4, dy + 20, dw - 16, C_DGRAY);
    vga12h_hline(dx + 4, dy + 26, dw - 20, C_DGRAY);
}

static void draw_icon_img_about(int x, int y) {
    int bx = x + 6, by = y + 2;
    int bw = ICON_IMG_W - 12, bh = ICON_IMG_H - 14;
    vga12h_rect  (bx,      by,      bw,     bh,     C_WHITE);
    vga12h_border(bx,      by,      bw,     bh,     C_BLACK);
    vga12h_pixel(bx,          by,          C_LGRAY);
    vga12h_pixel(bx + bw - 1, by,          C_LGRAY);
    vga12h_pixel(bx,          by + bh - 1, C_LGRAY);
    vga12h_pixel(bx + bw - 1, by + bh - 1, C_LGRAY);
    vga12h_string(bx + (bw - 8) / 2, by + (bh - 8) / 2, "i", C_BLUE, C_WHITE);
    vga12h_pixel(x + 14, y + ICON_IMG_H - 13, C_BLACK);
    vga12h_pixel(x + 12, y + ICON_IMG_H - 12, C_BLACK);
    vga12h_pixel(x + 10, y + ICON_IMG_H - 11, C_BLACK);
    vga12h_pixel(x + 14, y + ICON_IMG_H - 11, C_BLACK);
    vga12h_pixel(x + 16, y + ICON_IMG_H - 10, C_BLACK);
}

static void draw_icon_img_net(int x, int y) {
    // Placa de rede: retângulo PCB
    vga12h_rect  (x + 4,  y + 4,  ICON_IMG_W - 8, ICON_IMG_H - 16, C_LGREEN);
    vga12h_border(x + 4,  y + 4,  ICON_IMG_W - 8, ICON_IMG_H - 16, C_BLACK);
    // Chips na placa
    vga12h_rect  (x + 8,  y + 8,  10, 8, C_DGRAY);
    vga12h_border(x + 8,  y + 8,  10, 8, C_BLACK);
    vga12h_rect  (x + 24, y + 8,  10, 8, C_DGRAY);
    vga12h_border(x + 24, y + 8,  10, 8, C_BLACK);
    // Porta RJ45 na base
    vga12h_rect  (x + 16, y + ICON_IMG_H - 14, 16, 8, C_LGRAY);
    vga12h_border(x + 16, y + ICON_IMG_H - 14, 16, 8, C_BLACK);
    // Pinos da porta
    for (int i = 0; i < 4; i++)
        vga12h_vline(x + 18 + i*3, y + ICON_IMG_H - 12, 5, C_BLACK);
    // LED de link (verde)
    vga12h_rect(x + 8, y + ICON_IMG_H - 14, 5, 5,
                rtl8139_present() ? C_LGREEN : C_LGRAY);
}

static void draw_icon_img_browser(int x, int y) {
    vga12h_rect  (x + 4,  y + 2, ICON_IMG_W - 8, ICON_IMG_H - 8, C_LCYAN);
    vga12h_border(x + 4,  y + 2, ICON_IMG_W - 8, ICON_IMG_H - 8, C_BLUE);
    int cx = x + ICON_IMG_W/2, my = y + (ICON_IMG_H-8)/2 + 2;
    vga12h_hline(x + 4, my - 5, ICON_IMG_W - 8, C_BLUE);
    vga12h_hline(x + 4, my,     ICON_IMG_W - 8, C_BLUE);
    vga12h_hline(x + 4, my + 5, ICON_IMG_W - 8, C_BLUE);
    vga12h_vline(cx, y + 2, ICON_IMG_H - 8, C_BLUE);
    vga12h_rect  (x + 16, y + ICON_IMG_H - 6, 16, 4, C_DGRAY);
    vga12h_hline (x + 10, y + ICON_IMG_H - 2, 28, C_DGRAY);
    vga12h_hline (x + 10, y + ICON_IMG_H - 1, 28, C_BLACK);
}

static void draw_icon_img_mines(int x, int y) {
    vga12h_rect  (x+4,  y+2, ICON_IMG_W-8, ICON_IMG_H-6, 7);
    vga12h_border(x+4,  y+2, ICON_IMG_W-8, ICON_IMG_H-6, 8);
    // Mini-grade de minesweeper
    for (int i=0;i<3;i++) for(int j=0;j<3;j++) {
        int cx = x+6+j*10, cy = y+4+i*10;
        vga12h_rect  (cx, cy, 9, 9, 7);
        vga12h_border(cx, cy, 9, 9, 8);
    }
    // Bandeira no centro
    vga12h_vline(x+14, y+8, 7, 0);
    vga12h_rect (x+11, y+8, 5, 3, 4);
    // Mina no canto
    vga12h_rect (x+22, y+18, 4, 4, 0);
}

static void draw_icon_img_editor(int x, int y) {
    vga12h_rect  (x + 8,  y + 2, ICON_IMG_W - 14, ICON_IMG_H - 8, C_WHITE);
    vga12h_border(x + 8,  y + 2, ICON_IMG_W - 14, ICON_IMG_H - 8, C_DGRAY);
    int fx = x + ICON_IMG_W - 14;
    vga12h_rect  (fx, y+2, 6, 6, C_LGRAY);
    vga12h_border(fx, y+2, 6, 6, C_DGRAY);
    for (int i = 0; i < 4; i++)
        vga12h_hline(x+12, y+10+i*5, ICON_IMG_W-22, C_LGRAY);
    vga12h_rect  (x+5, y+ICON_IMG_H-12, 4, 10, C_YELLOW);
    vga12h_border(x+5, y+ICON_IMG_H-12, 4, 10, C_BROWN);
    vga12h_rect  (x+5, y+ICON_IMG_H-4,  4,  3, C_LGRAY);
}

static inline void icon_pos(int i, int *ox, int *oy) {
    *ox = ICON_START_X + i * ICON_STRIDE;
    *oy = ICON_START_Y;
}

static int hit_icon(int i, int mx_, int my_) {
    int ix, iy;
    icon_pos(i, &ix, &iy);
    return mx_ >= ix && mx_ < ix + ICON_IMG_W
        && my_ >= iy && my_ < iy + ICON_TOTAL_H;
}

static void draw_icon(int i) {
    int ix, iy;
    icon_pos(i, &ix, &iy);
    if (i == icon_selected)
        vga12h_rect(ix - 2, iy - 2, ICON_IMG_W + 4, ICON_TOTAL_H + 4, C_LBLUE);
    switch (i) {
	case 0: draw_icon_img_terminal(ix, iy); break;
	case 1: draw_icon_img_files   (ix, iy); break;
	case 2: draw_icon_img_net     (ix, iy); break;
	case 3: draw_icon_img_browser (ix, iy); break;
	case 4: draw_icon_img_mines   (ix, iy); break;
	case 5: draw_icon_img_editor  (ix, iy); break;
	case 6: draw_icon_img_about   (ix, iy); break;
    }
    const char *lbl = icon_defs[i].label;
    int lx  = ix + (ICON_IMG_W - vga12h_strpx(lbl)) / 2;
    int ly  = iy + ICON_IMG_H + ICON_GAP_Y;
    uint8_t lfg = (i == icon_selected) ? C_WHITE : C_BLACK;
    uint8_t lbg = (i == icon_selected) ? C_LBLUE : C_LCYAN;
    vga12h_string(lx, ly, lbl, lfg, lbg);
}

// ----------------------------------------------------------------
// Terminal gráfico
// ----------------------------------------------------------------
static void gterm_hook(char c) {
    if (c == '\r') { gt_out_col = 0; return; }
    if (c < 32 && c != '\n') return;

    if (c == '\n') {
        gt_out_col = 0;
        gt_out_row++;
        if (gt_out_row >= GTERM_ROWS) {
            gterm_scroll();
            gt_out_row = GTERM_ROWS - 1;
        }
        return;
    }

    // Quebra de linha automática
    if (gt_out_col >= GTERM_COLS) {
        gt_out_col = 0;
        gt_out_row++;
        if (gt_out_row >= GTERM_ROWS) {
            gterm_scroll();
            gt_out_row = GTERM_ROWS - 1;
        }
    }

    gt_buf[gt_out_row][gt_out_col++] = c;
    gt_buf[gt_out_row][gt_out_col]   = 0;
    if (gt_out_row < gt_dirty_from)
        gt_dirty_from = gt_out_row;
}

static void gterm_open(int win_id) {
    for (int r = 0; r < GTERM_ROWS; r++) gt_buf[r][0] = 0;
    gt_out_row = 0; gt_out_col = 0;
    gt_in[0] = 0; gt_in_len = 0; gt_in_cur = 0;
    gt_win_id = win_id;
    const char *msg = "FreeRootSD/OS  -  Terminal Balloon";
    for (int i = 0; msg[i]; i++) gterm_hook(msg[i]);
    gterm_hook('\n');
    gt_dirty_from = 0;
    const char *msg2 = "Digite 'help' para ver os comandos disponíveis.";
    for (int i = 0; msg2[i]; i++) gterm_hook(msg2[i]);
    gterm_hook('\n');
}

static void gterm_redraw_only(void) {
    if (gt_win_id < 0 || !windows[gt_win_id].active) return;
    Window *w = &windows[gt_win_id];

    // Coordenadas da área de conteúdo da janela
    int cx = w->x + 1;
    int cy = w->y + TITLEBAR_H + 1;
    int cw = w->w - 2;

    cursor_erase();

    // Redesenha só as linhas de output que mudaram
    if (gt_dirty_from < GTERM_ROWS) {
        int dy = cy + GT_PAD_V + gt_dirty_from * GT_LH;
        int dh = (GTERM_ROWS - gt_dirty_from) * GT_LH;
        vga12h_rect(cx, dy, cw, dh, GT_BG);
        for (int r = gt_dirty_from; r < GTERM_ROWS; r++) {
            if (!gt_buf[r][0]) continue;
            vga12h_string(cx + GT_PAD, cy + GT_PAD_V + r * GT_LH,
                          gt_buf[r], GT_FG, GT_BG);
        }
        gt_dirty_from = GTERM_ROWS;
    }

    // Redesenha só a linha de input (sempre, é barato)
    int sep_y  = cy + GT_PAD_V + GTERM_ROWS * GT_LH + 1;
    int in_y   = sep_y + 3;
    int in_w   = cw - GT_PAD * 2;
    int pw     = vga12h_strpx("> ");
    vga12h_rect  (cx + GT_PAD, in_y, in_w, GT_LH, GT_BG);
    vga12h_string(cx + GT_PAD, in_y, "> ", GT_PR_FG, GT_BG);
    int base_x = cx + GT_PAD + pw;
    for (int i = 0; i <= gt_in_len; i++) {
        int px = base_x + i * 8;
        if (px + 8 > cx + cw - GT_PAD) break;
        if (i == gt_in_cur) {
            char ch[2] = { (i < gt_in_len) ? gt_in[i] : ' ', 0 };
            vga12h_string(px, in_y, ch, GT_BG, GT_CUR_BG);
        } else if (i < gt_in_len) {
            char ch[2] = { gt_in[i], 0 };
            vga12h_string(px, in_y, ch, GT_IN_FG, GT_BG);
        }
    }

    cursor_draw(mouse_x(), mouse_y());
}

static void gterm_execute(char *cmd) {
    char *p = cmd;
    while (*p == ' ') p++;
    if (p[0]=='c' && p[1]=='l' && p[2]=='e' && p[3]=='a' && p[4]=='r' && !p[5]) {
        for (int r = 0; r < GTERM_ROWS; r++) gt_buf[r][0] = 0;
        gt_out_row = 0; gt_out_col = 0;
	gt_dirty_from = 0;
        return;
    }
    terminal_set_hook(gterm_hook);
    parse_and_execute(cmd);
    terminal_set_hook(0);
}

static void gt_hist_push(const char *cmd) {
    if (!cmd[0]) return;
    // não duplica o último
    if (gt_hist_count > 0) {
        int last = (gt_hist_head - 1 + GT_HIST_MAX) % GT_HIST_MAX;
        const char *a = gt_hist[last];
        int i = 0;
        while (a[i] && a[i] == cmd[i]) i++;
        if (!a[i] && !cmd[i]) return;
    }
    int i = 0;
    while (cmd[i] && i < GTERM_COLS) { gt_hist[gt_hist_head][i] = cmd[i]; i++; }
    gt_hist[gt_hist_head][i] = '\0';
    gt_hist_head = (gt_hist_head + 1) % GT_HIST_MAX;
    if (gt_hist_count < GT_HIST_MAX) gt_hist_count++;
}

static const char *gt_hist_get(int idx) {
    if (idx < 0 || idx >= gt_hist_count) return 0;
    int slot = (gt_hist_head - 1 - idx + GT_HIST_MAX * 2) % GT_HIST_MAX;
    return gt_hist[slot];
}

static void gterm_key(int key) {
    if (key < 0) return;
    if (key == '\n' || key == '\r') {
        gt_in[gt_in_len] = 0;
        gterm_hook('>'); gterm_hook(' ');
        for (int i = 0; i < gt_in_len; i++) gterm_hook(gt_in[i]);
        gterm_hook('\n');
	gt_hist_push(gt_in);     // ← linha nova
	gt_hist_idx = -1;        // ← linha nova
        if (gt_in_len > 0) gterm_execute(gt_in);
        gt_in[0] = 0; gt_in_len = 0; gt_in_cur = 0;
        gterm_redraw_only();
        return;
    }
    if (key == '\b') {
        if (gt_in_cur > 0) {
            for (int i = gt_in_cur-1; i < gt_in_len-1; i++) gt_in[i] = gt_in[i+1];
            gt_in_len--; gt_in_cur--;
            gt_in[gt_in_len] = 0;
            gterm_redraw_only();
        }
        return;
    }
    if (key == KEY_DEL) {
        if (gt_in_cur < gt_in_len) {
            for (int i = gt_in_cur; i < gt_in_len-1; i++) gt_in[i] = gt_in[i+1];
            gt_in_len--;
            gt_in[gt_in_len] = 0;
            gterm_redraw_only();
        }
        return;
    }
    if (key == KEY_LEFT)  { if (gt_in_cur > 0) { gt_in_cur--; gterm_redraw_only(); } return; }
    if (key == KEY_RIGHT) { if (gt_in_cur < gt_in_len) { gt_in_cur++; gterm_redraw_only(); } return; }
    if (key == KEY_HOME)  { gt_in_cur = 0; gterm_redraw_only(); return; }
    if (key == KEY_END)   { gt_in_cur = gt_in_len; gterm_redraw_only(); return; }
    if (key == KEY_ESC) {
        terminal_set_hook(0);
        int wid = gt_win_id;
        gt_win_id = -1;
        balloon_close(wid);
        return;
    }

    if (key == KEY_UP || key == KEY_DOWN) {
        if (key == KEY_UP && gt_hist_idx + 1 >= gt_hist_count) return;
        if (key == KEY_DOWN && gt_hist_idx == -1) return;
        // salva a linha atual antes de começar a navegar
        if (key == KEY_UP && gt_hist_idx == -1) {
            int i = 0;
            while (i < gt_in_len) { gt_saved[i] = gt_in[i]; i++; }
            gt_saved[i] = '\0';
        }
        gt_hist_idx += (key == KEY_UP) ? 1 : -1;
        const char *entry = (gt_hist_idx == -1) ? gt_saved : gt_hist_get(gt_hist_idx);
        gt_in_len = 0;
        while (entry[gt_in_len] && gt_in_len < GTERM_COLS)
            { gt_in[gt_in_len] = entry[gt_in_len]; gt_in_len++; }
        gt_in[gt_in_len] = '\0';
        gt_in_cur = gt_in_len;
        gterm_redraw_only();
        return;
    }

    if (key >= 32 && key <= 126 && gt_in_len < GTERM_COLS) {
        for (int i = gt_in_len; i > gt_in_cur; i--) gt_in[i] = gt_in[i-1];
        gt_in[gt_in_cur++] = (char)key;
        gt_in_len++;
        gt_in[gt_in_len] = 0;
        gterm_redraw_only();
    }
}

static void draw_gterm_content(int x, int y, int w, int h) {
    (void)h;
    vga12h_rect(x, y, w, h, GT_BG);
    for (int r = 0; r < GTERM_ROWS; r++) {
        if (!gt_buf[r][0]) continue;
        vga12h_string(x + GT_PAD, y + GT_PAD_V + r * GT_LH, gt_buf[r], GT_FG, GT_BG);
    }
    int sep_y = y + GT_PAD_V + GTERM_ROWS * GT_LH + 1;
    vga12h_hline(x + GT_PAD, sep_y, w - GT_PAD * 2, GT_SEP_C);
    int in_y  = sep_y + 3;
    int pw    = vga12h_strpx("> ");
    vga12h_string(x + GT_PAD, in_y, "> ", GT_PR_FG, GT_BG);
    int base_x = x + GT_PAD + pw;
    for (int i = 0; i <= gt_in_len; i++) {
        int px = base_x + i * 8;
        if (px + 8 > x + w - GT_PAD) break;
        if (i == gt_in_cur) {
            char ch[2] = { (i < gt_in_len) ? gt_in[i] : ' ', 0 };
            vga12h_string(px, in_y, ch, GT_BG, GT_CUR_BG);
        } else if (i < gt_in_len) {
            char ch[2] = { gt_in[i], 0 };
            vga12h_string(px, in_y, ch, GT_IN_FG, GT_BG);
        }
    }
}

// ----------------------------------------------------------------
// Explorador de arquivos
// ----------------------------------------------------------------
static void fexp_load_entries(void) {
    fexp.entry_count = 0;
    if (fexp.dir_idx != 0) {
        fexp.is_dir[fexp.entry_count] = 1;
        fexp.names[fexp.entry_count][0] = '.';
        fexp.names[fexp.entry_count][1] = '.';
        fexp.names[fexp.entry_count][2] = '\0';
        fexp.ext[fexp.entry_count][0] = '\0';
        fexp.entry_count++;
    }
    if (fexp.drive == 'A') {
        fsd_entry_t *t = fsd_table();
        for (int i = 1; i < fsd_max(); i++) {
            if (t[i].type != FSD_DIR) continue;
            if (t[i].parent != fexp.dir_idx) continue;
            if (fexp.entry_count >= 64) break;
            fexp.is_dir[fexp.entry_count] = 1;
            int j;
            for (j = 0; j < 9 && t[i].name[j] && t[i].name[j] != ' '; j++)
                fexp.names[fexp.entry_count][j] = t[i].name[j];
            fexp.names[fexp.entry_count][j] = '\0';
            fexp.ext[fexp.entry_count][0] = '\0';
            fexp.entry_count++;
        }
        for (int i = 1; i < fsd_max(); i++) {
            if (t[i].type != FSD_FILE) continue;
            if (t[i].parent != fexp.dir_idx) continue;
            if (fexp.entry_count >= 64) break;
            fexp.is_dir[fexp.entry_count] = 0;
            int j;
            for (j = 0; j < 9 && t[i].name[j] && t[i].name[j] != ' '; j++)
                fexp.names[fexp.entry_count][j] = t[i].name[j];
            fexp.names[fexp.entry_count][j] = '\0';
            for (j = 0; j < 4 && t[i].ext[j] && t[i].ext[j] != ' '; j++)
                fexp.ext[fexp.entry_count][j] = t[i].ext[j];
            fexp.ext[fexp.entry_count][j] = '\0';
            fexp.entry_count++;
        }
    } else {
        fs_entry_t *t = fs_table();
        for (int i = 1; i < fs_max(); i++) {
            if (t[i].type == FS_DIR && t[i].parent == fexp.dir_idx) {
                if (fexp.entry_count >= 64) break;
                fexp.is_dir[fexp.entry_count] = 1;
                int j;
                for (j = 0; j < 9 && t[i].name[j] && t[i].name[j] != ' '; j++)
                    fexp.names[fexp.entry_count][j] = t[i].name[j];
                fexp.names[fexp.entry_count][j] = '\0';
                fexp.ext[fexp.entry_count][0] = '\0';
                fexp.entry_count++;
            }
        }
        for (int i = 1; i < fs_max(); i++) {
            if (t[i].type == FS_FILE && t[i].parent == fexp.dir_idx) {
                if (fexp.entry_count >= 64) break;
                fexp.is_dir[fexp.entry_count] = 0;
                int j;
                for (j = 0; j < 9 && t[i].name[j] && t[i].name[j] != ' '; j++)
                    fexp.names[fexp.entry_count][j] = t[i].name[j];
                fexp.names[fexp.entry_count][j] = '\0';
                for (j = 0; j < 4 && t[i].ext[j] && t[i].ext[j] != ' '; j++)
                    fexp.ext[fexp.entry_count][j] = t[i].ext[j];
                fexp.ext[fexp.entry_count][j] = '\0';
                fexp.entry_count++;
            }
        }
    }
    if (fexp.selected >= fexp.entry_count) fexp.selected = -1;
    if (fexp.scroll > fexp.entry_count - 1) fexp.scroll = 0;
}

static void fexp_open_dir(int idx) {
    if (idx < 0 || idx >= fexp.entry_count) return;
    if (!fexp.is_dir[idx]) return;

    // Entrada virtual ".." – sobe para o diretório pai
    if (fexp.names[idx][0] == '.' && fexp.names[idx][1] == '.' && fexp.names[idx][2] == '\0') {
        if (fexp.drive == 'A') {
            fsd_entry_t *t = fsd_table();
            fexp.dir_idx = t[fexp.dir_idx].parent;
        } else {
            fs_entry_t *t = fs_table();
            fexp.dir_idx = t[fexp.dir_idx].parent;
        }
        fexp.scroll = 0;
        fexp.selected = -1;
        fexp_load_entries();
        balloon_redraw();
        return;
    }

    // Procura o índice do diretório na tabela do sistema de arquivos
    int found = -1;
    if (fexp.drive == 'A') {
        fsd_entry_t *t = fsd_table();
        for (int i = 1; i < fsd_max(); i++) {
            if (t[i].type != FSD_DIR) continue;
            if (t[i].parent != fexp.dir_idx) continue;
            // Comparação ignorando espaços e case-sensitive? Nomes de diretórios são UPPER.
            int eq = 1;
            for (int j = 0; j < 8; j++) { // nome tem 8 chars, sem extensão
                char a = t[i].name[j];
                char b = fexp.names[idx][j];
                if (a != b && a != ' ' && b != ' ') { eq = 0; break; }
            }
            if (eq) { found = i; break; }
        }
    } else {
        fs_entry_t *t = fs_table();
        for (int i = 1; i < fs_max(); i++) {
            if (t[i].type != FS_DIR) continue;
            if (t[i].parent != fexp.dir_idx) continue;
            int eq = 1;
            for (int j = 0; j < 8; j++) {
                char a = t[i].name[j];
                char b = fexp.names[idx][j];
                if (a != b && a != ' ' && b != ' ') { eq = 0; break; }
            }
            if (eq) { found = i; break; }
        }
    }

    if (found < 0) {
        // Opcional: exibir mensagem de erro
        return;
    }

    fexp.dir_idx = (uint8_t)found;
    fexp.scroll = 0;
    fexp.selected = -1;
    fexp_load_entries();
    balloon_redraw();
}

static void fexp_init_win(void) {
    fexp.active = 1;
    fexp.drive = 'A';
    fexp.dir_idx = 0;
    fexp.selected = -1;
    fexp.scroll = 0;
    fexp_load_entries();
}

static void draw_win_files(int x, int y, int w, int h) {
    (void)h;
    vga12h_rect(x, y, w, h, C_WHITE);
    vga12h_string(x + 4, y + 2, "Explorador de Arquivos", C_BLACK, C_WHITE);
    vga12h_hline(x + 2, y + 12, w - 4, C_DGRAY);

    int line_h = 12;
    int max_lines = (h - 20) / line_h;
    if (max_lines < 1) max_lines = 1;

    for (int i = 0; i < max_lines && fexp.scroll + i < fexp.entry_count; i++) {
        int idx = fexp.scroll + i;
        int py = y + 18 + i * line_h;
        uint8_t fg = (idx == fexp.selected) ? C_WHITE : C_BLACK;
        uint8_t bg = (idx == fexp.selected) ? C_BLUE : C_WHITE;
        char line[80];
        int pos = 0;
        if (fexp.is_dir[idx]) {
            line[pos++] = '['; line[pos++] = 'D'; line[pos++] = ']';
        } else {
            line[pos++] = '['; line[pos++] = 'F'; line[pos++] = ']';
        }
        line[pos++] = ' ';
        const char *n = fexp.names[idx];
        while (*n && pos < 70) line[pos++] = *n++;
        if (fexp.ext[idx][0]) {
            line[pos++] = '.';
            const char *e = fexp.ext[idx];
            while (*e && pos < 70) line[pos++] = *e++;
        }
        line[pos] = '\0';
        vga12h_string(x + 8, py, line, fg, bg);
    }

    char status[80];
    int spos = 0;
    status[spos++] = fexp.drive;
    status[spos++] = ':';
    if (fexp.dir_idx == 0) {
        status[spos++] = '/';
    } else {
        if (fexp.drive == 'A') {
            fsd_entry_t *t = fsd_table();
            const char *dn = t[fexp.dir_idx].name;
            while (*dn && spos < 78) status[spos++] = *dn++;
        } else {
            fs_entry_t *t = fs_table();
            const char *dn = t[fexp.dir_idx].name;
            while (*dn && spos < 78) status[spos++] = *dn++;
        }
    }
    status[spos] = '\0';
    vga12h_string(x + 4, y + h - 12, status, C_DGRAY, C_WHITE);
}

// ----------------------------------------------------------------
// Janela "Sobre"
// ----------------------------------------------------------------
static void draw_about_content(int x, int y, int w, int h) {
    (void)h;
    const char *name = "Balloon";
    int nx = x + (w - vga12h_strpx(name)) / 2;
    vga12h_string(nx, y + 6, name, C_BLACK, C_WHITE);
    vga12h_hline(x + 6, y + 18, w - 12, C_DGRAY);
    const char *lines[] = {
        "FreeRootSD/OS v0.6 Reforged",
        "Interface Balloon v0.2",
        "Modo VGA 12h  640x480",
        0
    };
    for (int i = 0; lines[i]; i++)
        vga12h_string(x + 8, y + 26 + i * 12, lines[i], C_DGRAY, C_WHITE);
}

// ----------------------------------------------------------------
// Interface de rede
// ----------------------------------------------------------------
// Imprime byte como decimal (sem terminal_writestring)
static void net_print_dec(int x, int y, uint8_t v, int fg) {
    char buf[4]; int i = 3; buf[3] = 0;
    if (v == 0) { buf[2] = '0'; i = 2; }
    else { while (v > 0) { buf[--i] = '0' + v%10; v /= 10; } }
    vga12h_string(x, y, buf + i, (uint8_t)fg, C_WHITE);
}

static void arp_gui(int x, int y, int lh, uint8_t bg) {
    const char *hex = "0123456789ABCDEF";
    int found = 0;
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (!arp_table[i].valid) continue;
        int cx = x;
        for (int j = 0; j < 4; j++) {
            uint8_t b = arp_table[i].ip[j];
            char buf[4]; int k = 3; buf[3] = 0;
            if (!b) { buf[2]='0'; k=2; }
            else { while (b) { buf[--k]='0'+b%10; b/=10; } }
            vga12h_string(cx, y, buf+k, C_BLUE, bg); cx += 24;
            if (j < 3) { vga12h_string(cx, y, ".", C_BLACK, bg); cx += 8; }
        }
        vga12h_string(cx, y, "->", C_BLACK, bg); cx += 18;
        for (int j = 0; j < 6; j++) {
            uint8_t m = arp_table[i].mac[j];
            char hx[3] = { hex[m>>4], hex[m&0xF], 0 };
            vga12h_string(cx, y, hx, C_DGRAY, bg); cx += 16;
            if (j < 5) { vga12h_string(cx, y, ":", C_BLACK, bg); cx += 8; }
        }
        y += lh; found++;
    }
    if (!found) vga12h_string(x, y, "Vazia", C_LGRAY, bg);
}

static void draw_net_content(int x, int y, int w, int h) {
    (void)w; (void)h;
    vga12h_rect(x, y, w, h, C_WHITE);
    const char *hex = "0123456789ABCDEF";
    int lh = 12, pad = 8, cx = x + pad, cy = y + 6;

    // ---- MAC ----
    vga12h_string(cx, cy, "MAC:", C_BLACK, C_WHITE);
    if (rtl8139_present()) {
        uint8_t mac[6]; rtl8139_get_mac(mac);
        int mx = cx + 40;
        for (int i = 0; i < 6; i++) {
            char hx[3] = { hex[mac[i]>>4], hex[mac[i]&0xF], 0 };
            vga12h_string(mx, cy, hx, C_LBLUE, C_WHITE); mx += 16;
            if (i < 5) { vga12h_string(mx, cy, ":", C_BLACK, C_WHITE); mx += 8; }
        }
    } else {
        vga12h_string(cx + 40, cy, "N/A", C_LGRAY, C_WHITE);
    }

    // ---- IP ----
    cy += lh;
    vga12h_string(cx, cy, "IP: ", C_BLACK, C_WHITE);
    int ix = cx + 40;
    for (int i = 0; i < 4; i++) {
        net_print_dec(ix, cy, net_ip[i], C_LBLUE); ix += 24;
        if (i < 3) { vga12h_string(ix, cy, ".", C_BLACK, C_WHITE); ix += 8; }
    }

    // ---- Gateway ----
    cy += lh;
    vga12h_string(cx, cy, "GW: ", C_BLACK, C_WHITE);
    ix = cx + 40;
    for (int i = 0; i < 4; i++) {
        net_print_dec(ix, cy, net_gw[i], C_LBLUE); ix += 24;
        if (i < 3) { vga12h_string(ix, cy, ".", C_BLACK, C_WHITE); ix += 8; }
    }

    // ---- Máscara ----
    cy += lh;
    vga12h_string(cx, cy, "MSK:", C_BLACK, C_WHITE);
    ix = cx + 40;
    for (int i = 0; i < 4; i++) {
        net_print_dec(ix, cy, net_mask[i], C_LBLUE); ix += 24;
        if (i < 3) { vga12h_string(ix, cy, ".", C_BLACK, C_WHITE); ix += 8; }
    }

    // ---- DNS ----
    cy += lh;
    vga12h_string(cx, cy, "DNS:", C_BLACK, C_WHITE);
    ix = cx + 40;
    uint8_t dns[4] = {10, 0, 2, 3};
    for (int i = 0; i < 4; i++) {
        net_print_dec(ix, cy, dns[i], C_LBLUE); ix += 24;
        if (i < 3) { vga12h_string(ix, cy, ".", C_BLACK, C_WHITE); ix += 8; }
    }

    // ---- Status ----
    cy += lh;
    vga12h_string(cx, cy, "Link:", C_BLACK, C_WHITE);
    if (rtl8139_present())
        vga12h_string(cx + 48, cy, "UP", C_LGREEN, C_WHITE);
    else
        vga12h_string(cx + 48, cy, "DOWN", C_RED, C_WHITE);

    // ---- Separador ----
    cy += lh + 2;
    vga12h_hline(cx, cy, w - pad*2, C_LGRAY);
    cy += 4;

    // ---- Tabela ARP ----
    vga12h_string(cx, cy, "Tabela ARP:", C_BLACK, C_WHITE);
    cy += lh;

    // Acessa tabela ARP diretamente via arp_table_for_gui
    // (precisamos de uma função auxiliar — ver abaixo)
    arp_gui(cx, cy, lh, C_WHITE);
}

// ----------------------------------------------------------------
// Editor de Texto Gráfico
// ----------------------------------------------------------------
// ---- Helpers de buffer ----
static int ed_llen(int r) {
    int l = 0; while (ed_lines[r][l]) l++; return l;
}

static void ed_ins_line(int idx) {
    if (ed_nlines >= ED_MAX_LINES) return;
    for (int i = ed_nlines; i > idx; i--)
        for (int j = 0; j <= ED_LINE_LEN; j++)
            ed_lines[i][j] = ed_lines[i-1][j];
    ed_lines[idx][0] = 0;
    ed_nlines++;
}

static void ed_del_line(int idx) {
    for (int i = idx; i < ed_nlines-1; i++)
        for (int j = 0; j <= ED_LINE_LEN; j++)
            ed_lines[i][j] = ed_lines[i+1][j];
    if (ed_nlines > 0) ed_nlines--;
}

static void ed_ensure_vis(int vis) {
    if (ed_row < ed_scroll) ed_scroll = ed_row;
    if (ed_row >= ed_scroll + vis) ed_scroll = ed_row - vis + 1;
    if (ed_scroll < 0) ed_scroll = 0;
}

static void ed_draw_status(int x, int y, int w, int bg) {
    vga12h_rect(x, y, w, 14, (uint8_t)bg);
    // Nome do arquivo
    char fname[16]; int fi = 0;
    if (ed_name[0]) {
        for (int i=0; ed_name[i]&&fi<8; i++) fname[fi++]=ed_name[i];
        if (ed_ext[0]) {
            fname[fi++]='.';
            for (int i=0; ed_ext[i]&&fi<12; i++) fname[fi++]=ed_ext[i];
        }
    } else {
        const char *u="Sem titulo"; for(int i=0;u[i];i++) fname[fi++]=u[i];
    }
    if (ed_modified) { fname[fi++]='*'; }
    fname[fi]=0;
    vga12h_string(x+4, y+3, fname, C_BLACK, (uint8_t)bg);
    // Linha:Coluna
    char pos[12]; int pi=0;
    uint8_t r=(uint8_t)(ed_row+1), c=(uint8_t)(ed_col+1);
    if (r>=100) pos[pi++]='0'+r/100; if (r>=10) pos[pi++]='0'+(r/10)%10; pos[pi++]='0'+r%10;
    pos[pi++]=':';
    if (c>=100) pos[pi++]='0'+c/100; if (c>=10) pos[pi++]='0'+(c/10)%10; pos[pi++]='0'+c%10;
    pos[pi]=0;
    vga12h_string(x+w-80, y+3, pos, C_DGRAY, (uint8_t)bg);
    // Atalhos
    const char *hint = ed_ask_save ? "Enter=OK  ESC=Cancelar"
                                   : "^S=Salvar  ^Q=Fechar";
    vga12h_string(x+w/2-60, y+3, hint, C_DGRAY, (uint8_t)bg);
}

static void ed_redraw_only(void) {
    if (ed_win_id < 0 || !windows[ed_win_id].active) return;
    Window *w = &windows[ed_win_id];
    int cx = w->x+1, cy = w->y+TITLEBAR_H+1;
    int cw = w->w-2, ch = w->h-TITLEBAR_H-2;
    int vis = (ch-14)/ED_LH;

    cursor_erase();

    // Repinta linha atual
    int ry = ed_row - ed_scroll;
    if (ry >= 0 && ry < vis) {
        int ly = cy + ry * ED_LH;
        vga12h_rect(cx, ly, cw, ED_LH, C_LGRAY);
        if (ed_lines[ed_row][0])
            vga12h_string(cx+4, ly, ed_lines[ed_row], C_BLACK, C_LGRAY);
        int lx = cx + 4 + ed_col * 8;
        if (lx < cx+cw-2) vga12h_vline(lx, ly, ED_LH, C_BLACK);
    }

    // Status + prompt de salvar
    if (ed_ask_save) {
        int sy = cy + ch - 14;
        vga12h_rect(cx, sy, cw, 14, C_YELLOW);
        vga12h_string(cx+4, sy+3, "Salvar como: ", C_BLACK, C_YELLOW);
        vga12h_string(cx+112, sy+3, ed_ask_buf, C_BLACK, C_YELLOW);
        int ax = cx+112+ed_ask_len*8;
        if (ax < cx+cw-4) vga12h_vline(ax, sy+3, 8, C_BLACK);
        vga12h_string(cx+cw-170, sy+3, "Enter=OK  ESC=Cancelar", C_DGRAY, C_YELLOW);
    } else {
        ed_draw_status(cx, cy+ch-14, cw, C_LGRAY);
    }

    cursor_draw(mouse_x(), mouse_y());
}

static void draw_editor_content(int x, int y, int w, int h) {
    vga12h_rect(x, y, w, h, C_WHITE);
    int vis = (h-14) / ED_LH;

    for (int row=0; row<vis && ed_scroll+row<ed_nlines; row++) {
        int r  = ed_scroll + row;
        int ly = y + row * ED_LH;
        int is_cur = (r == ed_row);
        if (is_cur) vga12h_rect(x, ly, w, ED_LH, C_LGRAY);
        if (ed_lines[r][0])
            vga12h_string(x+4, ly, ed_lines[r],
                          C_BLACK, is_cur ? C_LGRAY : C_WHITE);
        if (is_cur) {
            int lx = x + 4 + ed_col * 8;
            if (lx < x+w-2) vga12h_vline(lx, ly, ED_LH, C_BLACK);
        }
    }

    if (ed_ask_save) {
        int sy = y+h-14;
        vga12h_rect(x, sy, w, 14, C_YELLOW);
        vga12h_string(x+4, sy+3, "Salvar como: ", C_BLACK, C_YELLOW);
        vga12h_string(x+112, sy+3, ed_ask_buf, C_BLACK, C_YELLOW);
        int ax = x+112+ed_ask_len*8;
        if (ax<x+w-4) vga12h_vline(ax, sy+3, 8, C_BLACK);
    } else {
        ed_draw_status(x, y+h-14, w, C_LGRAY);
    }
}

static void ed_do_save(void) {
    if (!ed_name[0]) { ed_ask_save=1; ed_ask_buf[0]=0; ed_ask_len=0; return; }
    // Flatten buffer
    uint32_t pos = 0;
    for (int r=0; r<ed_nlines; r++) {
        int l=ed_llen(r);
        for (int j=0;j<l&&pos<(uint32_t)sizeof(ed_save_buf)-2;j++)
            ed_save_buf[pos++]=ed_lines[r][j];
        if (pos<(uint32_t)sizeof(ed_save_buf)-1) ed_save_buf[pos++]='\n';
    }
    ed_save_buf[pos]=0;
    if (vfs_write(ed_name, ed_ext, ed_save_buf, pos) >= 0)
        ed_modified = 0;
}

static void ed_load(const char *name, const char *ext) {
    const char *data = vfs_read(name, ext);
    ed_nlines=0; ed_row=0; ed_col=0; ed_scroll=0;
    if (!data) { ed_lines[0][0]=0; ed_nlines=1; return; }
    int l=0;
    while (*data) {
        if (*data=='\n'||*data=='\r') {
            ed_lines[l][0]=0; // already terminated
            if (ed_nlines < ED_MAX_LINES) ed_nlines++;
            l = ed_nlines-1;
            if (*data=='\r'&&*(data+1)=='\n') data++;
        } else if (ed_llen(l)<ED_LINE_LEN) {
            int len=ed_llen(l);
            ed_lines[l][len]=(char)*data;
            ed_lines[l][len+1]=0;
        }
        data++;
    }
    if (ed_nlines==0) ed_nlines=1;
}

static void ed_open(int win_id, const char *name, const char *ext) {
    ed_win_id  = win_id;
    ed_modified= 0;
    ed_ask_save= 0;
    for (int i=0;i<9;i++) ed_name[i]= name ? name[i] : 0;
    for (int i=0;i<4;i++) ed_ext[i]  = ext  ? ext[i]  : 0;
    if (name && name[0]) ed_load(name, ext);
    else { ed_lines[0][0]=0; ed_nlines=1; ed_row=0; ed_col=0; ed_scroll=0; }
}

static void ed_full_redraw(void) {
    if (ed_win_id<0) return;
    cursor_erase();
    draw_window(ed_win_id);
    cursor_draw(mouse_x(), mouse_y());
}

static void editor_key(int key) {
    // ---- Modo prompt de salvar ----
    if (ed_ask_save) {
        if (key==KEY_ESC) { ed_ask_save=0; ed_redraw_only(); return; }
        if (key=='\r'||key=='\n') {
            if (ed_ask_len>0) {
                // Split ask_buf em name.ext
                int di=-1;
                for (int i=0;i<ed_ask_len;i++) if (ed_ask_buf[i]=='.') di=i;
                int ni=0,ei=0;
                for (int i=0;i<(di>=0?di:ed_ask_len)&&ni<8;i++) ed_name[ni++]=ed_ask_buf[i];
                ed_name[ni]=0;
                if (di>=0) for(int i=di+1;i<ed_ask_len&&ei<3;i++) ed_ext[ei++]=ed_ask_buf[i];
                ed_ext[ei]=0;
            }
            ed_ask_save=0;
            ed_do_save();
            ed_full_redraw();
            return;
        }
        if ((key=='\b'||key==0x7F)&&ed_ask_len>0) ed_ask_buf[--ed_ask_len]=0;
        else if (key>=32&&key<128&&ed_ask_len<11){ ed_ask_buf[ed_ask_len++]=(char)key; ed_ask_buf[ed_ask_len]=0; }
        ed_redraw_only();
        return;
    }

    // ---- Modo edição normal ----
    Window *w   = &windows[ed_win_id];
    int vis     = (w->h - TITLEBAR_H - 2 - 14) / ED_LH;

    if (key==KEY_CTRL_S) { ed_do_save(); ed_redraw_only(); return; }
    if (key==KEY_CTRL_Q) { balloon_close(ed_win_id); balloon_redraw(); return; }

    if (key==KEY_UP)   { if(ed_row>0){ed_row--;int l=ed_llen(ed_row);if(ed_col>l)ed_col=l;} ed_ensure_vis(vis);ed_full_redraw();return;}
    if (key==KEY_DOWN) { if(ed_row<ed_nlines-1){ed_row++;int l=ed_llen(ed_row);if(ed_col>l)ed_col=l;} ed_ensure_vis(vis);ed_full_redraw();return;}
    if (key==KEY_LEFT) { if(ed_col>0)ed_col--;else if(ed_row>0){ed_row--;ed_col=ed_llen(ed_row);} ed_ensure_vis(vis);ed_full_redraw();return;}
    if (key==KEY_RIGHT){ int l=ed_llen(ed_row);if(ed_col<l)ed_col++;else if(ed_row<ed_nlines-1){ed_row++;ed_col=0;} ed_ensure_vis(vis);ed_full_redraw();return;}
    if (key==KEY_HOME) { ed_col=0; ed_redraw_only(); return;}
    if (key==KEY_END)  { ed_col=ed_llen(ed_row); ed_redraw_only(); return;}
    if (key==KEY_PGUP) { ed_row-=vis; if(ed_row<0)ed_row=0; ed_col=0; ed_ensure_vis(vis);ed_full_redraw();return;}
    if (key==KEY_PGDN) { ed_row+=vis; if(ed_row>=ed_nlines)ed_row=ed_nlines-1; ed_col=0; ed_ensure_vis(vis);ed_full_redraw();return;}

    if (key=='\r'||key=='\n') {
        // Quebra a linha em ed_col
        char rest[ED_LINE_LEN+1]; int rlen=ed_llen(ed_row)-ed_col;
        for(int i=0;i<rlen;i++) rest[i]=ed_lines[ed_row][ed_col+i]; rest[rlen]=0;
        ed_lines[ed_row][ed_col]=0;
        ed_ins_line(ed_row+1);
        for(int i=0;i<=rlen;i++) ed_lines[ed_row+1][i]=rest[i];
        ed_row++; ed_col=0; ed_modified=1;
        ed_ensure_vis(vis); ed_full_redraw(); return;
    }

    if (key=='\b'||key==0x7F) {
        if (ed_col>0) {
            int len=ed_llen(ed_row);
            for(int i=ed_col-1;i<len-1;i++) ed_lines[ed_row][i]=ed_lines[ed_row][i+1];
            ed_lines[ed_row][len-1]=0; ed_col--;
            ed_modified=1; ed_redraw_only();
        } else if (ed_row>0) {
            int prev=ed_llen(ed_row-1), cur=ed_llen(ed_row);
            if (prev+cur<=ED_LINE_LEN) {
                for(int i=0;i<cur;i++) ed_lines[ed_row-1][prev+i]=ed_lines[ed_row][i];
                ed_lines[ed_row-1][prev+cur]=0;
                ed_col=prev; ed_del_line(ed_row); ed_row--;
                ed_modified=1; ed_ensure_vis(vis); ed_full_redraw();
            }
        }
        return;
    }

    if (key==KEY_DEL) {
        int len=ed_llen(ed_row);
        if (ed_col<len) {
            for(int i=ed_col;i<len-1;i++) ed_lines[ed_row][i]=ed_lines[ed_row][i+1];
            ed_lines[ed_row][len-1]=0; ed_modified=1; ed_redraw_only();
        } else if (ed_row<ed_nlines-1) {
            int cur=len, next=ed_llen(ed_row+1);
            if (cur+next<=ED_LINE_LEN) {
                for(int i=0;i<next;i++) ed_lines[ed_row][cur+i]=ed_lines[ed_row+1][i];
                ed_lines[ed_row][cur+next]=0;
                ed_del_line(ed_row+1); ed_modified=1; ed_full_redraw();
            }
        }
        return;
    }

    // Caractere normal
    if (key>=32&&key<128) {
        int len=ed_llen(ed_row);
        if (len<ED_LINE_LEN) {
            for(int i=len;i>=ed_col;i--) ed_lines[ed_row][i+1]=ed_lines[ed_row][i];
            ed_lines[ed_row][ed_col++]=(char)key;
            ed_modified=1; ed_redraw_only();
        }
    }
}

// ----------------------------------------------------------------
// Botão direito do mouse
// ----------------------------------------------------------------
static void ctx_add(const char *label, int action) {
    if (ctx_nitems >= CTX_MAX_ITEMS) return;
    int i = 0;
    while (label[i] && i < 31) { ctx_items[ctx_nitems].label[i] = label[i]; i++; }
    ctx_items[ctx_nitems].label[i] = 0;
    ctx_items[ctx_nitems].action = action;
    ctx_nitems++;
}

static void ctx_draw(void) {
    int x = ctx_x, y = ctx_y;
    vga12h_rect  (x, y, ctx_w, ctx_h, C_WHITE);
    vga12h_border(x, y, ctx_w, ctx_h, C_DGRAY);
    for (int i = 0; i < ctx_nitems; i++) {
        int iy = y + 1 + i * CTX_ITEM_H;
        if (i == ctx_hover) {
            vga12h_rect  (x+1, iy, ctx_w-2, CTX_ITEM_H, C_LBLUE);
            vga12h_string(x+6, iy+3, ctx_items[i].label, C_WHITE, C_LBLUE);
        } else {
            vga12h_string(x+6, iy+3, ctx_items[i].label, C_BLACK, C_WHITE);
        }
    }
}

static int ctx_hit_item(int mx_, int my_) {
    if (!ctx_open) return -1;
    int x = ctx_x, y = ctx_y;
    if (mx_ < x || mx_ >= x+ctx_w || my_ < y || my_ >= y+ctx_h) return -1;
    int idx = (my_ - y - 1) / CTX_ITEM_H;
    return (idx >= 0 && idx < ctx_nitems) ? idx : -1;
}

static void ctx_close(void) {
    if (!ctx_open) return;
    cursor_erase();
    vga12h_restore(ctx_x, ctx_y, ctx_w, ctx_h, ctx_bg_buf);
    ctx_open = 0;
    cursor_draw(mouse_x(), mouse_y());
}

static void ctx_show(int x, int y) {
    ctx_nitems = ctx_nitems; // já preenchido antes de chamar
    ctx_w = CTX_ITEM_W;
    ctx_h = ctx_nitems * CTX_ITEM_H + 2;
    if (x + ctx_w > VGA12_W) x = VGA12_W - ctx_w;
    if (y + ctx_h > VGA12_H) y = VGA12_H - ctx_h;
    ctx_x = x; ctx_y = y;
    ctx_hover = -1;
    cursor_erase();
    vga12h_save(ctx_x, ctx_y, ctx_w, ctx_h, ctx_bg_buf);
    ctx_open = 1;
    ctx_draw();
    cursor_draw(mouse_x(), mouse_y());
}

static void ctx_execute(int action) {
    switch (action) {
        case CTX_OPEN_EDITOR: {
            if (ctx_fexp_idx < 0) break;
            BalloonWin wid = ed_win_id;
            if (wid < 0 || !windows[wid].active) {
                wid = balloon_open("Editor de Texto", 60, 60, 560, 340,
                                   draw_editor_content);
                if (wid == BALLOON_INVALID) break;
            }
            ed_open(wid, fexp.names[ctx_fexp_idx],
                    fexp.ext[ctx_fexp_idx][0] ? fexp.ext[ctx_fexp_idx] : 0);
            break;
        }
        case CTX_DELETE_FILE:
            if (ctx_fexp_idx >= 0 && !fexp.is_dir[ctx_fexp_idx]) {
                vfs_delete(fexp.names[ctx_fexp_idx], fexp.ext[ctx_fexp_idx], 0);
                fexp_load_entries();
            }
            break;
        case CTX_OPEN_DIR:
            if (ctx_fexp_idx >= 0) fexp_open_dir(ctx_fexp_idx);
            break;
        case CTX_CLOSE_WIN:
            if (ctx_win_id >= 0) balloon_close(ctx_win_id);
            break;
        case CTX_MIN_WIN:
            if (ctx_win_id >= 0)
                windows[ctx_win_id].minimized = !windows[ctx_win_id].minimized;
            break;
        case CTX_NEW_FILE: {
            BalloonWin wid = balloon_open("Editor de Texto", 60, 60, 560, 340,
                                          draw_editor_content);
            if (wid != BALLOON_INVALID) ed_open(wid, 0, 0);
            break;
        }
        case CTX_REFRESH:
            break;  // balloon_redraw() já é chamado após ctx_close
	case CTX_VIEW_IMAGE:
	    if (ctx_fexp_idx >= 0) {
	        bmp_view(fexp.names[ctx_fexp_idx],
	                 fexp.ext[ctx_fexp_idx][0] ? fexp.ext[ctx_fexp_idx] : 0);
	        balloon_redraw();
	    }
	    break;
    }
    balloon_redraw();
}

// Monta o menu baseado no que foi clicado
static void ctx_build(int mx_, int my_) {
    ctx_nitems     = 0;
    ctx_fexp_idx   = -1;
    ctx_win_id     = -1;

    // Verifica se está dentro do explorador de arquivos
    if (fexp_win_id >= 0 && windows[fexp_win_id].active) {
        Window *fw = &windows[fexp_win_id];
        int cx = mx_ - (fw->x + 1);
        int cy = my_ - (fw->y + TITLEBAR_H + 1);
        if (cx >= 0 && cx < fw->w-2 && cy >= 0 && cy < fw->h-TITLEBAR_H-2) {
            int entry_idx = (cy - 14) / 12;
            int real_idx  = fexp.scroll + entry_idx;
            if (real_idx >= 0 && real_idx < fexp.entry_count) {
                ctx_fexp_idx = real_idx;
                ctx_win_id   = fexp_win_id;
		if (!fexp.is_dir[real_idx]) {
		    // Verifica se é BMP
		    int is_bmp = (fexp.ext[real_idx][0]=='B'||fexp.ext[real_idx][0]=='b') &&
		                 (fexp.ext[real_idx][1]=='M'||fexp.ext[real_idx][1]=='m') &&
                		 (fexp.ext[real_idx][2]=='P'||fexp.ext[real_idx][2]=='p');
		    if (is_bmp) ctx_add("Abrir Imagem", CTX_VIEW_IMAGE);
		    ctx_add("Abrir no Editor", CTX_OPEN_EDITOR);
		    ctx_add("Excluir Arquivo", CTX_DELETE_FILE);
		}
                if (fexp.is_dir[real_idx]) {
                    ctx_add("Abrir Diretorio", CTX_OPEN_DIR);
                } else {
                    ctx_add("Abrir no Editor", CTX_OPEN_EDITOR);
                    ctx_add("Excluir Arquivo", CTX_DELETE_FILE);
                }
                ctx_add("Fechar Janela",  CTX_CLOSE_WIN);
                return;
            }
        }
    }

    // Verifica se está sobre uma janela qualquer
    for (int i = num_windows-1; i >= 0; i--) {
        if (!windows[i].active) continue;
        Window *w = &windows[i];
        if (mx_ >= w->x && mx_ < w->x+w->w &&
            my_ >= w->y && my_ < w->y+w->h) {
            ctx_win_id = i;
            ctx_add("Fechar Janela",  CTX_CLOSE_WIN);
            ctx_add("Minimizar",      CTX_MIN_WIN);
            return;
        }
    }

    // Desktop
    ctx_add("Novo Arquivo",  CTX_NEW_FILE);
    ctx_add("Atualizar",     CTX_REFRESH);
}

// ----------------------------------------------------------------
// Browser Texto HTML Simples
// ----------------------------------------------------------------
// ---- HTML renderer ----
static int br_tag_sw(const char *tag, int tlen, const char *s) {
    for (int i = 0; s[i]; i++) {
        if (i >= tlen) return 0;
        char a = tag[i]; if (a>='A'&&a<='Z') a+=32;
        char b = s[i];   if (b>='A'&&b<='Z') b+=32;
        if (a != b) return 0;
    }
    return 1;
}

static char  br_cur[BR_LINE_LEN + 1];
static int   br_cpos, br_sws;

static void br_flush(void) {
    if (br_nlines >= BR_MAX_LINES) return;
    br_cur[br_cpos] = 0;
    for (int i = 0; i <= br_cpos; i++) br_lines[br_nlines][i] = br_cur[i];
    br_nlines++; br_cpos = 0; br_sws = 1;
}

static void br_push(char c) {
    if (br_cpos >= BR_LINE_LEN) {
        int w = br_cpos - 1;
        while (w > 0 && br_cur[w] != ' ') w--;
        if (w > 0) {
            br_cur[w] = 0;
            if (br_nlines < BR_MAX_LINES) {
                for (int i = 0; i <= w; i++) br_lines[br_nlines][i] = br_cur[i];
                br_nlines++;
            }
            int rest = br_cpos - w - 1;
            for (int i = 0; i < rest; i++) br_cur[i] = br_cur[w+1+i];
            br_cpos = rest;
        } else { br_flush(); }
    }
    br_cur[br_cpos++] = c;
}

static void br_render_html(const uint8_t *data, uint32_t len) {
    br_nlines = 0; br_scroll = 0; br_cpos = 0; br_sws = 1;
    uint32_t i = 0;
    int skip = 0;

    while (i < len) {
        uint8_t c = data[i];
        if (c == '<') {
            uint32_t ts = i + 1;
            while (i < len && data[i] != '>') i++;
            int tl = (int)(i - ts);
            if (i < len) i++;
            const char *tag = (const char *)data + ts;

            if (br_tag_sw(tag,tl,"script")||br_tag_sw(tag,tl,"style"))  { skip=1; continue; }
            if (br_tag_sw(tag,tl,"/script")||br_tag_sw(tag,tl,"/style")){ skip=0; continue; }
            if (skip) continue;

            if (br_tag_sw(tag,tl,"br")||br_tag_sw(tag,tl,"br/")||
                br_tag_sw(tag,tl,"tr")||br_tag_sw(tag,tl,"li")) {
                if (br_cpos > 0) br_flush();
            } else if (br_tag_sw(tag,tl,"p")||br_tag_sw(tag,tl,"/p")||
                       br_tag_sw(tag,tl,"div")||br_tag_sw(tag,tl,"/div")||
                       (tl>=2&&(tag[0]=='h'||tag[0]=='H')&&tag[1]>='1'&&tag[1]<='6')||
                       (tl>=3&&tag[0]=='/'&&(tag[1]=='h'||tag[1]=='H')&&tag[2]>='1'&&tag[2]<='6')) {
                if (br_cpos>0) br_flush();
                if (br_nlines>0&&br_lines[br_nlines-1][0]!=0) br_flush();
            }
            continue;
        }
        i++;
        if (skip) continue;

        // Entidade
        if (c == '&') {
            char ent[8]={0}; int ei=0;
            while (i<len&&data[i]!=';'&&data[i]!='<'&&ei<7) ent[ei++]=(char)data[i++];
            if (i<len&&data[i]==';') i++;
            if (ent[0]=='a'&&ent[1]=='m'&&ent[2]=='p') c='&';
            else if (ent[0]=='l'&&ent[1]=='t') c='<';
            else if (ent[0]=='g'&&ent[1]=='t') c='>';
            else if (ent[0]=='n'&&ent[1]=='b'&&ent[2]=='s'&&ent[3]=='p') c=' ';
            else c=' ';
        }

        if (c=='\n'||c=='\r'||c=='\t') c=' ';
        if (c==' ') { if (!br_sws&&br_cpos>0){br_push(' ');br_sws=1;} continue; }
        br_sws = 0;
        if (c >= 32 && c < 128) br_push((char)c);
    }
    if (br_cpos > 0) br_flush();
}

// ---- Navegação ----
static void br_show_msg(const char *msg) {
    br_nlines = 1; br_scroll = 0;
    for (int i = 0; msg[i] && i < BR_LINE_LEN; i++) br_lines[0][i] = msg[i];
    int l = 0; while (msg[l]) l++;
    br_lines[0][l] = 0;
}

static void br_navigate(void) {
    const char *url = br_url;
    if (url[0]=='h'&&url[1]=='t'&&url[2]=='t'&&url[3]=='p'&&
        url[4]==':'&&url[5]=='/'&&url[6]=='/') url += 7;

    char host[64]={0}; int hi=0;
    while (url[hi]&&url[hi]!='/'&&hi<63) { host[hi]=url[hi]; hi++; }
    const char *path = (url[hi]=='/') ? url+hi : "/";

    br_show_msg("Carregando...");
    if (br_win_id>=0) { cursor_erase(); draw_window(br_win_id); cursor_draw(mouse_x(),mouse_y()); }

    uint8_t ip[4];
    if (!dns_resolve(host, ip, 3000)) { br_show_msg("Erro: DNS falhou."); return; }

    static TcpConn bconn;
    if (!tcp_connect(&bconn, ip, 80, 5000)) { br_show_msg("Erro: Falha TCP."); return; }

    static char req[256]; int rp=0;
    const char *p1="GET ",*p2=" HTTP/1.0\r\nHost: ",*p3="\r\n\r\n";
    for(int j=0;p1[j];j++) req[rp++]=p1[j];
    for(int j=0;path[j];j++) req[rp++]=path[j];
    for(int j=0;p2[j];j++) req[rp++]=p2[j];
    for(int j=0;host[j];j++) req[rp++]=host[j];
    for(int j=0;p3[j];j++) req[rp++]=p3[j];
    tcp_send(&bconn, (const uint8_t *)req, (uint16_t)rp);

    uint32_t total=0; uint8_t chunk[512]; int n;
    while ((n=tcp_recv(&bconn,chunk,512,3000))>0) {
        if (total+(uint32_t)n>sizeof(br_fetch_buf)) n=(int)(sizeof(br_fetch_buf)-total);
        for(int j=0;j<n;j++) br_fetch_buf[total+j]=chunk[j];
        total+=(uint32_t)n;
        if (total>=sizeof(br_fetch_buf)) break;
    }
    tcp_close(&bconn);

    uint32_t body=0;
    for(uint32_t j=0;j+3<total;j++) {
        if(br_fetch_buf[j]=='\r'&&br_fetch_buf[j+1]=='\n'&&
           br_fetch_buf[j+2]=='\r'&&br_fetch_buf[j+3]=='\n')
            { body=j+4; break; }
    }
    br_render_html(br_fetch_buf+body, total-body);
}

static void browser_redraw_url_only(void) {
    if (br_win_id < 0 || !windows[br_win_id].active) return;
    Window *w = &windows[br_win_id];
    int cx = w->x + 1;
    int cy = w->y + TITLEBAR_H + 1;
    int cw = w->w - 2;

    cursor_erase();

    // Repinta só a barra de URL (14px de altura + 2px de padding)
    vga12h_rect  (cx + 2, cy + 2, cw - 4, 14, C_LGRAY);
    vga12h_border(cx + 2, cy + 2, cw - 4, 14, C_DGRAY);
    if (br_url_len > 0)
        vga12h_string(cx + 6, cy + 4, br_url, C_BLACK, C_LGRAY);
    else
        vga12h_string(cx + 6, cy + 4, "http://exemplo.com", C_DGRAY, C_LGRAY);
    int ucx = cx + 6 + br_url_len * 8;
    if (ucx < cx + cw - 6) vga12h_vline(ucx, cy + 4, 8, C_BLACK);

    cursor_draw(mouse_x(), mouse_y());
}

static void browser_key(int key) {
    if (key == KEY_UP)   { if (br_scroll > 0) br_scroll--;           goto br_full; }
    if (key == KEY_DOWN) { if (br_scroll + 1 < br_nlines) br_scroll++; goto br_full; }
    if (key == '\r' || key == '\n') {
        br_navigate();
        goto br_full;
    }
    if (key == '\b' || key == 0x7F) {
        if (br_url_len > 0) br_url[--br_url_len] = 0;
    } else if (key >= 32 && key < 128 && br_url_len < 119) {
        br_url[br_url_len++] = (char)key;
        br_url[br_url_len]   = 0;
    }
    browser_redraw_url_only();  // ← só a URL bar
    return;

    br_full:
    if (br_win_id >= 0) {
        cursor_erase();
        draw_window(br_win_id);
        cursor_draw(mouse_x(), mouse_y());
    }
}

// ---- Conteúdo da janela ----
static void draw_browser_content(int x, int y, int w, int h) {
    vga12h_rect(x, y, w, h, C_WHITE);

    // Barra de URL
    int uh = 14;
    vga12h_rect  (x+2, y+2, w-4, uh, C_LGRAY);
    vga12h_border(x+2, y+2, w-4, uh, C_DGRAY);
    if (br_url_len > 0)
        vga12h_string(x+6, y+4, br_url, C_BLACK, C_LGRAY);
    else
        vga12h_string(x+6, y+4, "http://exemplo.com", C_DGRAY, C_LGRAY);
    int ucx = x + 6 + br_url_len * 8;
    if (ucx < x+w-6) vga12h_vline(ucx, y+4, 8, C_BLACK);

    vga12h_hline(x, y+uh+4, w, C_LGRAY);

    int cy = y+uh+6, avail = h-uh-8, vis = avail/BR_LH;

    if (br_nlines == 0) {
        vga12h_string(x+8, cy+10, "Digite um URL e pressione Enter", C_LGRAY, C_WHITE);
        return;
    }

    for (int row=0; row<vis && br_scroll+row<br_nlines; row++) {
        if (br_lines[br_scroll+row][0])
            vga12h_string(x+4, cy+row*BR_LH, br_lines[br_scroll+row], C_BLACK, C_WHITE);
    }

    if (br_nlines > vis) {
        int sx = x+w-5;
        vga12h_rect(sx, cy, 4, avail, C_LGRAY);
        int th = avail*vis/br_nlines; if (th<6) th=6;
        int ty = cy + (avail-th)*br_scroll/(br_nlines-vis);
        vga12h_rect(sx, ty, 4, th, C_DGRAY);
    }
}

// ----------------------------------------------------------------
// Barra de menus
// ----------------------------------------------------------------
static void draw_menubar(void) {
    vga12h_rect (0, 0, VGA12_W, MENUBAR_H - 1, C_YELLOW);
    vga12h_hline(0, MENUBAR_H - 1, VGA12_W,    C_BLACK);

    for (int i = 0; menu_items[i].label; i++) {
        uint8_t fg = C_BLACK, bg = C_YELLOW;
        if (i == 0 && dropdown_open) {
            int iw = vga12h_strpx(menu_items[i].label) + 4;
            vga12h_rect(menu_items[i].x - 2, 0, iw, MENUBAR_H - 1, C_BLACK);
            fg = C_YELLOW; bg = C_BLACK;
        }
        vga12h_string(menu_items[i].x, 3, menu_items[i].label, fg, bg);
    }

    int rh, rm, rs;
    rtc_get_time(&rh, &rm, &rs);
    char clk[13];
    fmt_time_12h(rh, rm, rs, clk);
    int cx = VGA12_W - vga12h_strpx(clk) - 6;
    vga12h_string(cx, 3, clk, C_BLACK, C_WHITE);
}

static void redraw_clock_only(void) {
    int rh, rm, rs;
    rtc_get_time(&rh, &rm, &rs);
    char clk[13];
    fmt_time_12h(rh, rm, rs, clk);
    int cw = vga12h_strpx(clk);
    int cx = VGA12_W - cw - 6;
    int restore = 0;
    if (cur_sx >= 0 && cur_sx + CUR_W > cx - 1 && cur_sx < VGA12_W && cur_sy < MENUBAR_H) {
        cursor_erase();
        restore = 1;
    }
    vga12h_rect  (cx - 1, 0, cw + 7, MENUBAR_H - 1, C_YELLOW);
    vga12h_string(cx,     3, clk,     C_BLACK, C_YELLOW);
    if (restore) cursor_draw(mouse_x(), mouse_y());
}

// ----------------------------------------------------------------
// Dropdown
// ----------------------------------------------------------------
static void draw_dropdown(void) {
    if (!dropdown_open) return;
    vga12h_rect  (DROPDOWN_X, DD_TOP, DROPDOWN_W, DROPDOWN_H, C_WHITE);
    vga12h_border(DROPDOWN_X, DD_TOP, DROPDOWN_W, DROPDOWN_H, C_BLACK);
    vga12h_string(DROPDOWN_X + 8, DD_ABOUT_Y, "Sobre o Balloon", C_BLACK, C_WHITE);
    vga12h_hline (DROPDOWN_X + 4, DD_SEP_Y,   DROPDOWN_W - 8,   C_DGRAY);
    vga12h_string(DROPDOWN_X + 8, DD_QUIT_Y,  "Reiniciar e Sair",            C_BLACK, C_WHITE);
}

// ----------------------------------------------------------------
// Desktop
// ----------------------------------------------------------------
static void draw_desktop(void) {
    vga12h_rect(0, MENUBAR_H, VGA12_W, VGA12_H - MENUBAR_H, C_CYAN);
    for (int i = 0; i < ICON_COUNT; i++)
        draw_icon(i);
}

// ----------------------------------------------------------------
// Titlebar, close box, janela
// ----------------------------------------------------------------
static void draw_titlebar(int x, int y, int w, int active) {
    vga12h_rect(x + 1, y, w - 2, TITLEBAR_H, C_LGREEN);
    if (active) {
        for (int row = 1; row < TITLEBAR_H; row += 2)
            vga12h_hline(x + 1, y + row, w - 2, C_GREEN);
    }
}

static void draw_closebox(int x, int y) {
    vga12h_rect  (x, y, CLOSEBOX_SZ, CLOSEBOX_SZ, C_LRED);
    vga12h_border(x, y, CLOSEBOX_SZ, CLOSEBOX_SZ, C_BLACK);
    vga12h_pixel(x+3, y+3, C_WHITE); vga12h_pixel(x+7, y+3, C_WHITE);
    vga12h_pixel(x+4, y+4, C_WHITE); vga12h_pixel(x+6, y+4, C_WHITE);
    vga12h_pixel(x+5, y+5, C_WHITE);
    vga12h_pixel(x+4, y+6, C_WHITE); vga12h_pixel(x+6, y+6, C_WHITE);
    vga12h_pixel(x+3, y+7, C_WHITE); vga12h_pixel(x+7, y+7, C_WHITE);
}

static void draw_minbox(int x, int y) {
    vga12h_rect  (x, y, MINBOX_SZ, MINBOX_SZ, C_LBLUE);
    vga12h_border(x, y, MINBOX_SZ, MINBOX_SZ, C_BLACK);
    // traço horizontal no centro
    vga12h_hline(x + 2, y + MINBOX_SZ / 2, MINBOX_SZ - 4, C_WHITE);
}

static void draw_maxbox(int x, int y) {
    vga12h_rect  (x, y, MAXBOX_SZ, MAXBOX_SZ, C_LGREEN);
    vga12h_border(x, y, MAXBOX_SZ, MAXBOX_SZ, C_BLACK);
    // Quadrado interno (símbolo de maximizar)
    vga12h_border(x + 2, y + 2, MAXBOX_SZ - 4, MAXBOX_SZ - 4, C_BLACK);
}

static void draw_window(int i) {
    Window *w = &windows[i];
    if (!w->active) return;
    int x = w->x, y = w->y, ww = w->w, wh = w->h;

    if (w->minimized) {
        // Minimizado: só titlebar + sombra curta
        vga12h_rect  (x + 4, y + 4, ww, TITLEBAR_H, C_LCYAN);
        draw_titlebar(x, y, ww, 1);
        int cb_y = y + (TITLEBAR_H - CLOSEBOX_SZ) / 2;
        draw_closebox(x + 4, cb_y);
        draw_minbox  (x + 4 + MINBOX_OFF, cb_y);
	draw_maxbox  (x + 4 + MAXBOX_OFF, cb_y);   // ← novo
        int tw = vga12h_strpx(w->title);
        if (tw > 0) {
            int tx = x + (ww - tw) / 2;
            int ty = y + (TITLEBAR_H - 8) / 2;
            vga12h_rect  (tx - 1, ty - 1, tw + 2, 10, C_LGREEN);
            vga12h_string(tx, ty, w->title, C_BLACK, C_LGREEN);
        }
        vga12h_border(x, y, ww, TITLEBAR_H, C_BLACK);
        return;
    }

    // Normal: janela completa
    vga12h_rect  (x + 4, y + 4, ww, wh, C_LCYAN);
    vga12h_rect  (x + 1, y + TITLEBAR_H + 1, ww - 2, wh - TITLEBAR_H - 2, C_WHITE);
    draw_titlebar(x, y, ww, 1);

    int cb_y = y + (TITLEBAR_H - CLOSEBOX_SZ) / 2;
    draw_closebox(x + 4, cb_y);
    draw_minbox  (x + 4 + MINBOX_OFF, cb_y);
    draw_maxbox  (x + 4 + MAXBOX_OFF, cb_y);   // ← novo

    int tw = vga12h_strpx(w->title);
    if (tw > 0) {
        int tx = x + (ww - tw) / 2;
        int ty = y + (TITLEBAR_H - 8) / 2;
        vga12h_rect  (tx - 1, ty - 1, tw + 2, 10, C_LGREEN);
        vga12h_string(tx, ty, w->title, C_BLACK, C_LGREEN);
    }

    vga12h_border(x, y, ww, wh, C_BLACK);
    vga12h_hline (x + 1, y + TITLEBAR_H, ww - 2, C_BLACK);

    if (w->draw_fn)
        w->draw_fn(x + 1, y + TITLEBAR_H + 1, ww - 2, wh - TITLEBAR_H - 2);

    // Grip direito
    int gx = x + ww - RESIZE_SZ - 1;
    int gy = y + wh - RESIZE_SZ - 1;
    for (int d = 2; d < RESIZE_SZ; d += 2)
        vga12h_hline(gx + d, gy + d, RESIZE_SZ - d, C_LGRAY);

    // Grip esquerdo
    int glx = x + 1;
    for (int d = 2; d < RESIZE_SZ; d += 2)
        vga12h_hline(glx, gy + d, RESIZE_SZ - d, C_LGRAY);
}

// ----------------------------------------------------------------
// VBlank + redraw
// ----------------------------------------------------------------
static inline void wait_vblank(void) {
    while ( inb(0x3DA) & 0x08) {}
    while (!(inb(0x3DA) & 0x08)) {}
}

void balloon_redraw(void) {
    wait_vblank();
    cursor_erase();
    draw_desktop();
    draw_menubar();
    for (int i = 0; i < BALLOON_MAX_WINDOWS; i++)
        draw_window(i);
    draw_dropdown();
    cursor_draw(mouse_x(), mouse_y());
}

// ----------------------------------------------------------------
// API pública
// ----------------------------------------------------------------
void balloon_init(void) {
    for (int i = 0; i < BALLOON_MAX_WINDOWS; i++)
        windows[i].active = 0;
    num_windows      = 0;
    dropdown_open    = 0;
    icon_selected    = -1;
    icon_last_clicked = -1;
    gt_win_id        = -1;
    fexp_win_id      = -1;
    mouse_init();
    balloon_redraw();
}

BalloonWin balloon_open(const char *title, int x, int y, int w, int h,
                        BalloonDraw draw_fn) {
    if (num_windows >= BALLOON_MAX_WINDOWS) return BALLOON_INVALID;
    int i = num_windows;
    windows[i].active  = 1;
    windows[i].x = x; windows[i].y = y;
    windows[i].w = w; windows[i].h = h;
    windows[i].draw_fn = draw_fn;
    int j = 0;
    while (title[j] && j < 31) { windows[i].title[j] = title[j]; j++; }
    windows[i].title[j] = '\0';
    num_windows++;
    adlib_note_on(0, 784, 15, ADLIB_PIANO);
    balloon_redraw();
    return i;
}

void balloon_close(BalloonWin id) {
    if (id < 0 || id >= num_windows || !windows[id].active) return;

    if (gt_win_id   == id) gt_win_id   = -1;
    if (fexp_win_id == id) fexp_win_id = -1;
    if (drag_id     == id) drag_id     = -1;  // ← volta aqui
    if (br_win_id   == id) br_win_id = -1;
    if (ed_win_id == id)   ed_win_id = -1;

    windows[id].active = 0;
    num_windows--;

    if (id < num_windows) {
        windows[id] = windows[num_windows];
        windows[num_windows].active = 0;
        if (gt_win_id   == num_windows) gt_win_id   = id;
        if (fexp_win_id == num_windows) fexp_win_id = id;
        if (drag_id     == num_windows) drag_id     = id;  // ← e aqui
        if (br_win_id   == num_windows) br_win_id   = id;
	if (ed_win_id   == num_windows) ed_win_id   = id;
        if (ms_win_id   == num_windows) ms_win_id   = id;
    }
    adlib_note_on(0, 523, 15, ADLIB_PIANO);
    balloon_redraw();
}

static void icon_open_window(int idx) {
    const IconDef *def = &icon_defs[idx];
    for (int i = 0; i < BALLOON_MAX_WINDOWS; i++) {
        if (!windows[i].active) continue;
        const char *t = windows[i].title, *r = def->win_title;
        int eq = 1;
        for (int k = 0; r[k] || t[k]; k++)
            if (t[k] != r[k]) { eq = 0; break; }
        if (eq) return;
    }
    int wx = (VGA12_W - def->win_w) / 2 + idx * 20;
    int wy = (VGA12_H - def->win_h) / 2 + idx * 16;
    if (wx < 0) wx = 0;
    if (wy < MENUBAR_H) wy = MENUBAR_H;
    BalloonWin wid = balloon_open(def->win_title, wx, wy, def->win_w, def->win_h, def->draw_content);

    if (idx == 0 && wid != BALLOON_INVALID) {
        gterm_open(wid);
    }
    if (idx == 1 && wid != BALLOON_INVALID) {
        fexp_win_id = wid;
        fexp_init_win();
    }
    if (idx == 3 && wid != BALLOON_INVALID) {
        br_win_id = wid;
        br_nlines = 0;
        br_scroll = 0;
        // Não pré-preenche — usuário digita do zero
        br_url[0]   = 0;
        br_url_len  = 0;
    }
    if (idx == 4 && wid != BALLOON_INVALID) {
        ms_win_id = wid;
        ms_init();
    }
    if (idx == 5 && wid != BALLOON_INVALID)
        ed_open(wid, 0, 0);
}

// ----------------------------------------------------------------
// Hit tests
// ----------------------------------------------------------------
static int hit_titlebar(int id, int mx_, int my_) {
    Window *w = &windows[id];
    return w->active
        && mx_ >= w->x && mx_ < w->x + w->w
        && my_ >= w->y && my_ < w->y + TITLEBAR_H;
}

static int hit_closebox(int id, int mx_, int my_) {
    Window *w = &windows[id];
    int cb_y = w->y + (TITLEBAR_H - CLOSEBOX_SZ) / 2;
    return w->active
        && mx_ >= w->x + 4 && mx_ < w->x + 4 + CLOSEBOX_SZ
        && my_ >= cb_y     && my_ < cb_y + CLOSEBOX_SZ;
}

static int hit_minbox(int id, int mx_, int my_) {
    Window *w = &windows[id];
    if (!w->active) return 0;
    int cb_y = w->y + (TITLEBAR_H - MINBOX_SZ) / 2;
    return mx_ >= w->x + 4 + MINBOX_OFF
        && mx_ <  w->x + 4 + MINBOX_OFF + MINBOX_SZ
        && my_ >= cb_y
        && my_ <  cb_y + MINBOX_SZ;
}

static int hit_maxbox(int id, int mx_, int my_) {
    Window *w = &windows[id];
    if (!w->active) return 0;
    int cb_y = w->y + (TITLEBAR_H - MAXBOX_SZ) / 2;
    return mx_ >= w->x + 4 + MAXBOX_OFF
        && mx_ <  w->x + 4 + MAXBOX_OFF + MAXBOX_SZ
        && my_ >= cb_y
        && my_ <  cb_y + MAXBOX_SZ;
}

static int hit_resizebox(int id, int mx_, int my_) {
    Window *w = &windows[id];
    if (!w->active || w->minimized) return 0;
    int gy = w->y + w->h - RESIZE_SZ;
    if (my_ < gy || my_ >= w->y + w->h) return 0;
    // canto direito
    if (mx_ >= w->x + w->w - RESIZE_SZ && mx_ < w->x + w->w) return 1;
    // canto esquerdo
    if (mx_ >= w->x && mx_ < w->x + RESIZE_SZ) return 2;
    return 0;
}

static int hit_menu_balloon(int mx_, int my_) {
    int lw = vga12h_strpx(menu_items[0].label);
    return my_ >= 0 && my_ < MENUBAR_H
        && mx_ >= menu_items[0].x - 2
        && mx_ <  menu_items[0].x + lw + 2;
}

static int hit_dd_quit(int mx_, int my_) {
    return mx_ >= DROPDOWN_X && mx_ < DROPDOWN_X + DROPDOWN_W
        && my_ >= DD_QUIT_TOP && my_ < DD_QUIT_BOT;
}

static int hit_dd_about(int mx_, int my_) {
    return mx_ >= DROPDOWN_X && mx_ < DROPDOWN_X + DROPDOWN_W
        && my_ >= DD_ABOUT_TOP && my_ < DD_ABOUT_BOT;
}

static int hit_dd_area(int mx_, int my_) {
    return mx_ >= DROPDOWN_X && mx_ < DROPDOWN_X + DROPDOWN_W
        && my_ >= DD_TOP      && my_ < DD_BOTTOM;
}

static int bring_to_front(int id) {
    if (id < 0 || id >= num_windows) return id;
    if (id == num_windows - 1) return id;

    Window tmp = windows[id];
    for (int i = id; i < num_windows - 1; i++)
        windows[i] = windows[i + 1];
    windows[num_windows - 1] = tmp;

    if      (gt_win_id == id)           gt_win_id = num_windows - 1;
    else if (gt_win_id > id)            gt_win_id--;

    if      (fexp_win_id == id)         fexp_win_id = num_windows - 1;
    else if (fexp_win_id > id)          fexp_win_id--;

    if      (br_win_id == id)           br_win_id = num_windows - 1;
    else if (br_win_id > id)            br_win_id--;

    if      (ed_win_id == id)           ed_win_id = num_windows - 1;
    else if (ed_win_id > id)            ed_win_id--;

    if      (ms_win_id == id)           ms_win_id = num_windows - 1;
    else if (ms_win_id > id)            ms_win_id--;

    balloon_redraw();
    return num_windows - 1;
}
// ----------------------------------------------------------------
// Loop de eventos
// ----------------------------------------------------------------
void balloon_run(void) {
    static int drag_id = -1;
    static int drag_ox = 0, drag_oy = 0;
    int btn_prev = 0;
    uint32_t last_tick = 0;
    int last_click_time = 0;
    int last_click_idx = -1;

    while (1) {
        // ---- Teclado ----
	while (keyboard_available()) {
	    int key = getchar_nonblock();
	    if (key < 0) break;

	    // 1. Teclas para o terminal gráfico (se ativo)
	    if (gt_win_id >= 0 && windows[gt_win_id].active) {
	        gterm_key(key);
	    }
	    // 2. Teclas para o navegador (se ativo)
	    else if (br_win_id >= 0 && windows[br_win_id].active) {
	        browser_key(key);
	    }
  	    // 3. Teclas para o editor (se ativo)
	    else if (ed_win_id >= 0 && windows[ed_win_id].active) {
	        editor_key(key);
            }
	    // 4. Tecla ESC sem janelas específicas: fecha dropdown ou última janela
	    else if (key == KEY_ESC) {
	        if (dropdown_open) {
	            dropdown_open = 0;
	            balloon_redraw();
	        } else {
	            for (int i = BALLOON_MAX_WINDOWS - 1; i >= 0; i--) {
	                if (windows[i].active) {
	                    balloon_close(i);
	                    break;
	                }
        	    }
	        }
	    }
	}

        // ---- Atualiza relógio ----
	if (!mouse_moved()) {
	    asm volatile ("hlt");
	    if (timer_get_ticks() - last_tick >= 1000) {
	        last_tick = timer_get_ticks();
	        redraw_clock_only();
	    }
	    continue;
	}

        int mx_ = mouse_x();
        int my_ = mouse_y();
        int btn = mouse_buttons();

	// ---- Menu de contexto aberto ----
	if (ctx_open) {
	    int hover = ctx_hit_item(mx_, my_);
	    if (hover != ctx_hover) {
	        ctx_hover = hover;
	        cursor_erase();
	        ctx_draw();
	        cursor_draw(mx_, my_);
	    }
	    if ((btn & MOUSE_BTN_LEFT) && !(btn_prev & MOUSE_BTN_LEFT)) {
	        int sel = ctx_hit_item(mx_, my_);
	        ctx_close();
	        if (sel >= 0) ctx_execute(ctx_items[sel].action);
	        btn_prev = btn;
	        continue;
	    }
	    if ((btn & MOUSE_BTN_RIGHT) && !(btn_prev & MOUSE_BTN_RIGHT)) {
	        ctx_close();
	        btn_prev = btn;
	        continue;
	    }
	}

	// ---- Botão direito ----
	if ((btn & MOUSE_BTN_RIGHT) && !(btn_prev & MOUSE_BTN_RIGHT)) {
	    int handled = 0;

	    // Campo Minado tem prioridade absoluta
	    if (ms_win_id >= 0 && windows[ms_win_id].active && !windows[ms_win_id].minimized) {
	        Window *mw = &windows[ms_win_id];
	        int mcx = mw->x + 1;
	        int mcy = mw->y + TITLEBAR_H + 1;
	        int mcw = mw->w - 2;
	        int mch = mw->h - TITLEBAR_H - 2;
	        if (mx_ >= mcx && mx_ < mcx + mcw && my_ >= mcy && my_ < mcy + mch) {
	            ms_click(mx_, my_, mcx, mcy, mcw, mch, 1);
	            balloon_redraw();
	            handled = 1;
	        }
	    }

	    if (!handled) {
	        ctx_build(mx_, my_);
	        if (ctx_nitems > 0) ctx_show(mx_, my_);
	    }
	}

        // ---- Borda de descida do botão esquerdo ----
        if ((btn & MOUSE_BTN_LEFT) && !(btn_prev & MOUSE_BTN_LEFT)) {

            // 1. Menu Balloon
            if (hit_menu_balloon(mx_, my_)) {
                dropdown_open = !dropdown_open;
                balloon_redraw();
                goto done_click;
            }

            // 2. Dropdown
            if (dropdown_open) {
                if (hit_dd_quit(mx_, my_)) {
                    dropdown_open = 0;
                    return;
                }
                if (hit_dd_about(mx_, my_)) {
                    dropdown_open = 0;
                    icon_selected = -1;
                    icon_open_window(2);
                    balloon_redraw();
                    goto done_click;
                }
                if (!hit_dd_area(mx_, my_)) {
                    dropdown_open = 0;
                    balloon_redraw();
                }
                goto done_click;
            }

            // 3. Close boxes
            for (int i = BALLOON_MAX_WINDOWS - 1; i >= 0; i--) {
                if (hit_closebox(i, mx_, my_)) {
                    if (i == gt_win_id) {
                        terminal_set_hook(0);
                        gt_win_id = -1;
                    }
                    if (i == fexp_win_id) {
                        fexp_win_id = -1;
                    }
                    balloon_close(i);
                    goto done_click;
                }
		// Minimizar / restaurar janela
		if (hit_minbox(i, mx_, my_)) {
		    windows[i].minimized = !windows[i].minimized;
		    balloon_redraw();
		    goto done_click;
                }
		// Maximizar / restaurar janela
		if (hit_maxbox(i, mx_, my_)) {
		    Window *w = &windows[i];
		    if (!w->maximized) {
		        // Salva posição atual e maximiza
		        w->saved_x = w->x; w->saved_y = w->y;
		        w->saved_w = w->w; w->saved_h = w->h;
		        w->x = 0;
		        w->y = MENUBAR_H;
		        w->w = VGA12_W;
		        w->h = VGA12_H - MENUBAR_H;
		        w->maximized = 1;
		    } else {
		        // Restaura
		        w->x = w->saved_x; w->y = w->saved_y;
		        w->w = w->saved_w; w->h = w->saved_h;
		        w->maximized = 0;
		    }
		    w->minimized = 0;
		    balloon_redraw();
		    goto done_click;
		}
            }

	    // Redimensionar janela
	    for (int i = num_windows - 1; i >= 0; i--) {
	        if (!windows[i].active || windows[i].minimized) continue;
	        int side = hit_resizebox(i, mx_, my_);
	        if (side) {
	            int new_id = bring_to_front(i);
	            resize_id   = new_id;
	            resize_left = (side == 2);
	            resize_ox   = windows[new_id].x + windows[new_id].w - mx_;
      	            resize_oy   = windows[new_id].y + windows[new_id].h - my_;
        	    goto done_click;
                }
	    }

	    // 4. Arrastar janela pela titlebar
	    for (int i = BALLOON_MAX_WINDOWS - 1; i >= 0; i--) {
	        if (hit_titlebar(i, mx_, my_)) {
	            int new_id = bring_to_front(i);
	            drag_id = new_id;
	            drag_ox = mx_ - windows[new_id].x;
	            drag_oy = my_ - windows[new_id].y;
	            goto done_click;
	        }
	    }

            // 5. Cliques nos ícones do desktop
            {
                int clicked = -1;
                for (int i = 0; i < ICON_COUNT; i++)
                    if (hit_icon(i, mx_, my_)) { clicked = i; break; }
                if (clicked >= 0) {
                    if (clicked == icon_last_clicked) {
                        icon_selected     = -1;
                        icon_last_clicked = -1;
                        icon_open_window(clicked);
                        balloon_redraw();
                    } else {
                        icon_selected     = clicked;
                        icon_last_clicked = clicked;
                        balloon_redraw();
                    }
                    goto done_click;
                }
                if (icon_selected >= 0) {
                    icon_selected     = -1;
                    icon_last_clicked = -1;
                    balloon_redraw();
                }
            }

	    if (ms_win_id >= 0 && windows[ms_win_id].active && !windows[ms_win_id].minimized) {
	        Window *mw = &windows[ms_win_id];
	        int mcx = mw->x + 1;
	        int mcy = mw->y + TITLEBAR_H + 1;
	        int mcw = mw->w - 2;
	        int mch = mw->h - TITLEBAR_H - 2;
		if (mx_ >= mcx && mx_ < mcx + mcw && my_ >= mcy && my_ < mcy + mch) {
	            // Verifica se clicou no smile
	            int smile_x = mcx + (mcw / 2) - 10;
	            int smile_y = mcy + 6;
	            if (mx_ >= smile_x && mx_ < smile_x + 20 && my_ >= smile_y && my_ < smile_y + 20) {
	                ms_init();
	                balloon_redraw();
	                goto done_click;
	            }
	            // Caso contrário, processa a célula
	            ms_click(mx_, my_, mcx, mcy, mcw, mch, 0);
	            balloon_redraw();
	            goto done_click;
	        }
	    }

            // 6. Clique na área de conteúdo do explorador de arquivos
            if (fexp_win_id >= 0 && windows[fexp_win_id].active) {
                Window *w = &windows[fexp_win_id];
                int cx = mx_ - (w->x + 1);
                int cy = my_ - (w->y + TITLEBAR_H + 1);
                if (cx >= 0 && cx < w->w - 2 && cy >= 0 && cy < w->h - TITLEBAR_H - 2) {
                    int line_h = 12;
                    int entry_idx = (cy - 14) / line_h;
                    if (entry_idx >= 0 && entry_idx < (w->h - 20) / line_h) {
                        int real_idx = fexp.scroll + entry_idx;
                        if (real_idx >= 0 && real_idx < fexp.entry_count) {
                            fexp.selected = real_idx;
                            balloon_redraw();
                            int now = timer_get_ticks();
                            if (last_click_idx == real_idx && (now - last_click_time) < 1500) {
                                if (fexp.is_dir[real_idx]) {
                                    fexp_open_dir(real_idx);
                                } else {
                                    terminal_writestring("Arquivo: ");
                                    terminal_writestring(fexp.names[real_idx]);
                                    if (fexp.ext[real_idx][0]) {
                                        terminal_putchar('.');
                                        terminal_writestring(fexp.ext[real_idx]);
                                    }
                                    terminal_putchar('\n');
                                }
                            }
                            last_click_time = now;
                            last_click_idx = real_idx;
                            goto done_click;
                        }
                    }
                }
            }
            done_click:;     // ← linha 2083 original, não adicione outra
        }                    // ← fecha o if (btn & BTN_LEFT)

	// ---- Arrastar janela ----
	if (drag_id >= 0 && (btn & MOUSE_BTN_LEFT) &&
	    !windows[drag_id].maximized) {

	    int nx = mx_ - drag_ox;
	    int ny = my_ - drag_oy;
	    if (nx < 0) nx = 0;
	    if (ny < MENUBAR_H) ny = MENUBAR_H;
	    if (nx + windows[drag_id].w > VGA12_W) nx = VGA12_W - windows[drag_id].w;
	    if (ny + windows[drag_id].h > VGA12_H) ny = VGA12_H - windows[drag_id].h;

	    cursor_erase();

	    if (nx != windows[drag_id].x || ny != windows[drag_id].y) {
	        int ox = windows[drag_id].x;
	        int oy = windows[drag_id].y;
	        int ow = windows[drag_id].w + 4;  // +4 cobre a sombra
        	int oh = windows[drag_id].h + 4;

	        // 1. Repinta desktop na posição antiga (só escritas — rápido)
	        int ex = ox, ey = oy, ew = ow, eh = oh;
	        if (ex < 0)          { ew += ex; ex = 0; }
	        if (ey < MENUBAR_H)  { eh -= (MENUBAR_H - ey); ey = MENUBAR_H; }
	        if (ex + ew > VGA12_W) ew = VGA12_W - ex;
	        if (ey + eh > VGA12_H) eh = VGA12_H - ey;
	        if (ew > 0 && eh > 0)
	            vga12h_rect(ex, ey, ew, eh, C_CYAN);

	        // 2. Redesenha ícones que estavam sob a janela
        	for (int i = 0; i < ICON_COUNT; i++) {
	            int ix, iy;
	            icon_pos(i, &ix, &iy);
        	    if (ix < ox + ow && ix + ICON_IMG_W  > ox &&
	                iy < oy + oh && iy + ICON_TOTAL_H > oy)
        	        draw_icon(i);
	        }

	        // 3. Redesenha janelas que ficaram expostas (as que estão atrás)
	        for (int i = 0; i < BALLOON_MAX_WINDOWS; i++) {
	            if (i == drag_id || !windows[i].active) continue;
	            Window *bw_ = &windows[i];
	            if (bw_->x < ox + ow && bw_->x + bw_->w > ox &&
	                bw_->y < oy + oh && bw_->y + bw_->h > oy)
	                draw_window(i);
	        }

	        // 4. Desenha a janela na nova posição
        	windows[drag_id].x = nx;
	        windows[drag_id].y = ny;
	        draw_window(drag_id);
	    }

	    cursor_draw(mx_, my_);
	}

	// ---- Redimensionar janela ----
	if (resize_id >= 0 && (btn & MOUSE_BTN_LEFT) &&
	    !windows[resize_id].maximized) {

	    cursor_erase();
	    if (resize_left) {

	        // Arrasta borda esquerda: x e w mudam juntos
	        int new_x  = mx_;
	        int new_w  = windows[resize_id].x + windows[resize_id].w - new_x;
	        int max_x  = windows[resize_id].x + windows[resize_id].w - WIN_MIN_W;
	        if (new_x < 0)      new_x = 0;
	        if (new_x > max_x)  new_x = max_x;
	        new_w = windows[resize_id].x + windows[resize_id].w - new_x;
	        int nh = my_ + resize_oy - windows[resize_id].y;
	        if (nh < WIN_MIN_H) nh = WIN_MIN_H;
	        if (windows[resize_id].y + nh > VGA12_H) nh = VGA12_H - windows[resize_id].y;
	        if (new_x != windows[resize_id].x || nh != windows[resize_id].h) {
	            windows[resize_id].x = new_x;
	            windows[resize_id].w = new_w;
	            windows[resize_id].h = nh;
	            balloon_redraw();
	        }
	    } else {
	        // Arrasta borda direita: só w e h mudam
	        int nw = mx_ + resize_ox - windows[resize_id].x;
	        int nh = my_ + resize_oy - windows[resize_id].y;
	        if (nw < WIN_MIN_W) nw = WIN_MIN_W;
	        if (nh < WIN_MIN_H) nh = WIN_MIN_H;
	        if (windows[resize_id].x + nw > VGA12_W) nw = VGA12_W - windows[resize_id].x;
	        if (windows[resize_id].y + nh > VGA12_H) nh = VGA12_H - windows[resize_id].y;
        	if (nw != windows[resize_id].w || nh != windows[resize_id].h) {
	            windows[resize_id].w = nw;
	            windows[resize_id].h = nh;
	            balloon_redraw();
	        }
	    }
	    cursor_draw(mx_, my_);
	}

	// ---- Botão solto ----
	if (!(btn & MOUSE_BTN_LEFT) && (btn_prev & MOUSE_BTN_LEFT)) {
	    if (drag_id >= 0)   balloon_redraw();
	    if (resize_id >= 0) { balloon_redraw(); resize_id = -1; }
	    drag_id = -1;
	}

	// ---- Move cursor ----
	if (drag_id < 0) {
	    cursor_erase();
	    cursor_draw(mx_, my_);
	}
        btn_prev = btn;
    }
}
