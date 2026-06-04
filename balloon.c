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
#define ICON_COUNT     3

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
static void draw_about_content(int x, int y, int w, int h);
static void draw_gterm_content (int x, int y, int w, int h);
static void draw_win_files    (int x, int y, int w, int h);

typedef struct {
    const char *label;
    const char *win_title;
    int         win_w, win_h;
    BalloonDraw draw_content;
} IconDef;

static const IconDef icon_defs[ICON_COUNT] = {
    { "Terminal", "Terminal",        610, 306, draw_gterm_content },
    { "Arquivos", "Arquivos",        500, 300, draw_win_files },
    { "Sobre",    "Sobre o Balloon", 280, 120, draw_about_content },
};

static int icon_selected    = -1;
static int icon_last_clicked = -1;

// ----------------------------------------------------------------
// Terminal gráfico (declarações antecipadas)
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

static char  gt_buf[GTERM_ROWS][GTERM_COLS + 1];
static int   gt_out_row;
static int   gt_out_col;
static char  gt_in[GTERM_COLS + 1];
static int   gt_in_len;
static int   gt_in_cur;
static int   gt_win_id = -1;

static void gterm_hook(char c);
static void gterm_open(int win_id);
static void gterm_key(int key);
static void gterm_redraw_only(void);
static void draw_gterm_content(int x, int y, int w, int h);

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
static int fexp_win_id = -1;

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
        case 2: draw_icon_img_about   (ix, iy); break;
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
    if (c == '\n') { gt_out_col = 0; gt_out_row++; if (gt_out_row >= GTERM_ROWS) gt_out_row = GTERM_ROWS-1; return; }
    if (c == '\r') { gt_out_col = 0; return; }
    if (c < 32) return;
    if (gt_out_col >= GTERM_COLS) { gt_out_col = 0; gt_out_row++; }
    if (gt_out_row < GTERM_ROWS) {
        gt_buf[gt_out_row][gt_out_col++] = c;
        gt_buf[gt_out_row][gt_out_col] = 0;
    }
}

static void gterm_open(int win_id) {
    for (int r = 0; r < GTERM_ROWS; r++) gt_buf[r][0] = 0;
    gt_out_row = 0; gt_out_col = 0;
    gt_in[0] = 0; gt_in_len = 0; gt_in_cur = 0;
    gt_win_id = win_id;
    const char *msg = "FreeRootSD/OS  -  Terminal Balloon";
    for (int i = 0; msg[i]; i++) gterm_hook(msg[i]);
    gterm_hook('\n');
    const char *msg2 = "Digite 'help' para ver os comandos disponíveis.";
    for (int i = 0; msg2[i]; i++) gterm_hook(msg2[i]);
    gterm_hook('\n');
}

static void gterm_redraw_only(void) {
    if (gt_win_id < 0 || !windows[gt_win_id].active) return;
    cursor_erase();
    draw_window(gt_win_id);
    cursor_draw(mouse_x(), mouse_y());
}

static void gterm_execute(char *cmd) {
    char *p = cmd;
    while (*p == ' ') p++;
    if (p[0]=='c' && p[1]=='l' && p[2]=='e' && p[3]=='a' && p[4]=='r' && !p[5]) {
        for (int r = 0; r < GTERM_ROWS; r++) gt_buf[r][0] = 0;
        gt_out_row = 0; gt_out_col = 0;
        return;
    }
    terminal_set_hook(gterm_hook);
    parse_and_execute(cmd);
    terminal_set_hook(0);
}

static void gterm_key(int key) {
    if (key < 0) return;
    if (key == '\n' || key == '\r') {
        gt_in[gt_in_len] = 0;
        gterm_hook('>'); gterm_hook(' ');
        for (int i = 0; i < gt_in_len; i++) gterm_hook(gt_in[i]);
        gterm_hook('\n');
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
    int in_y   = sep_y + 3;
    int pw     = vga12h_strpx("> ");
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
        "FreeRootSD/OS v0.5 Ultimate",
        "Interface Balloon v0.1",
        "Modo VGA 12h  640x480",
        0
    };
    for (int i = 0; lines[i]; i++)
        vga12h_string(x + 8, y + 26 + i * 12, lines[i], C_DGRAY, C_WHITE);
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
    vga12h_rect  (x, y, CLOSEBOX_SZ, CLOSEBOX_SZ, C_LBLUE);
    vga12h_border(x, y, CLOSEBOX_SZ, CLOSEBOX_SZ, C_BLACK);
    vga12h_pixel(x+3, y+3, C_BLACK); vga12h_pixel(x+7, y+3, C_BLACK);
    vga12h_pixel(x+4, y+4, C_BLACK); vga12h_pixel(x+6, y+4, C_BLACK);
    vga12h_pixel(x+5, y+5, C_BLACK);
    vga12h_pixel(x+4, y+6, C_BLACK); vga12h_pixel(x+6, y+6, C_BLACK);
    vga12h_pixel(x+3, y+7, C_BLACK); vga12h_pixel(x+7, y+7, C_BLACK);
}

static void draw_window(int i) {
    Window *w = &windows[i];
    if (!w->active) return;
    int x = w->x, y = w->y, ww = w->w, wh = w->h;

    vga12h_rect  (x + 4, y + 4, ww, wh, C_LCYAN);
    vga12h_rect  (x + 1, y + TITLEBAR_H + 1, ww - 2, wh - TITLEBAR_H - 2, C_WHITE);
    draw_titlebar(x, y, ww, 1);

    int cb_y = y + (TITLEBAR_H - CLOSEBOX_SZ) / 2;
    draw_closebox(x + 4, cb_y);

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
    for (int i = 0; i < BALLOON_MAX_WINDOWS; i++) {
        if (!windows[i].active) {
            windows[i].active  = 1;
            windows[i].x = x; windows[i].y = y;
            windows[i].w = w; windows[i].h = h;
            windows[i].draw_fn = draw_fn;
            int j = 0;
            while (title[j] && j < 31) { windows[i].title[j] = title[j]; j++; }
            windows[i].title[j] = '\0';
            num_windows++;
            balloon_redraw();
            return i;
        }
    }
    return BALLOON_INVALID;
}

void balloon_close(BalloonWin id) {
    if (id < 0 || id >= BALLOON_MAX_WINDOWS) return;
    windows[id].active = 0;
    num_windows--;
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
    if (id == num_windows - 1) return id; // já está no topo
    Window tmp = windows[id];
    for (int i = id; i < num_windows - 1; i++)
        windows[i] = windows[i+1];
    windows[num_windows - 1] = tmp;
    balloon_redraw();
    return num_windows - 1;
}

// ----------------------------------------------------------------
// Loop de eventos
// ----------------------------------------------------------------
void balloon_run(void) {
    int drag_id  = -1;
    int drag_ox  = 0, drag_oy = 0;
    int btn_prev = 0;
    int last_sec = -1;
    int last_click_time = 0;
    int last_click_idx = -1;

    while (1) {
        // ---- Teclado ----
        while (keyboard_available()) {
            int key = getchar_nonblock();
            if (key < 0) break;
            if (gt_win_id >= 0 && windows[gt_win_id].active) {
                gterm_key(key);
            } else if (key == KEY_ESC) {
                if (dropdown_open) {
                    dropdown_open = 0;
                    balloon_redraw();
                } else {
                    for (int i = BALLOON_MAX_WINDOWS - 1; i >= 0; i--) {
                        if (windows[i].active) { balloon_close(i); break; }
                    }
                }
            }
        }

        // ---- Atualiza relógio ----
        if (!mouse_moved()) {
            asm volatile ("hlt");
            int rh, rm, rs;
            rtc_get_time(&rh, &rm, &rs);
            if (rs != last_sec) {
                last_sec = rs;
                redraw_clock_only();
            }
            continue;
        }

        int mx_ = mouse_x();
        int my_ = mouse_y();
        int btn = mouse_buttons();

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
            }

            // 4. Arrastar janela pela titlebar
            for (int i = BALLOON_MAX_WINDOWS - 1; i >= 0; i--) {
                if (hit_titlebar(i, mx_, my_)) {
		    int new_id = bring_to_front(i);
                    drag_id = i;
                    drag_ox = mx_ - windows[i].x;
                    drag_oy = my_ - windows[i].y;
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

            done_click:;
        }

        // ---- Arrastar janela ----
        if (drag_id >= 0 && (btn & MOUSE_BTN_LEFT)) {
            int nx = mx_ - drag_ox;
            int ny = my_ - drag_oy;
            if (nx < 0) nx = 0;
            if (ny < MENUBAR_H) ny = MENUBAR_H;
            if (nx + windows[drag_id].w > VGA12_W) nx = VGA12_W - windows[drag_id].w;
            if (ny + windows[drag_id].h > VGA12_H) ny = VGA12_H - windows[drag_id].h;
            windows[drag_id].x = nx;
            windows[drag_id].y = ny;
            balloon_redraw();
        }

        // ---- Botão solto ----
        if (!(btn & MOUSE_BTN_LEFT) && (btn_prev & MOUSE_BTN_LEFT))
            drag_id = -1;

        // ---- Move cursor ----
        if (drag_id < 0) {
            cursor_erase();
            cursor_draw(mx_, my_);
        }

        btn_prev = btn;
    }
}
