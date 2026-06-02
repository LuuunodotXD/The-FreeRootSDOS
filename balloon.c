// balloon.c — interface gráfica Balloon
// Estilo: Macintosh original (barra de menus no topo, janelas com título listrado)
// Modo: VGA 12h — 640×480, 16 cores

#include "balloon.h"
#include "vga12h.h"
#include "mouse.h"
#include "keyboard.h"
#include "io.h"
#include <stdint.h>

// ----------------------------------------------------------------
// Paleta de cores (16 cores VGA padrão, índices idênticos ao modo 13h)
// ----------------------------------------------------------------
#define C_BLACK   0
#define C_WHITE  15
#define C_LGRAY   7   // desktop, conteúdo de janela
#define C_DGRAY   8   // sombra, detalhes

// ----------------------------------------------------------------
// Layout — escalado para 640×480
// ----------------------------------------------------------------
#define MENUBAR_H    14   // altura da barra de menus (px)
#define TITLEBAR_H   13   // altura da barra de título da janela
#define CLOSEBOX_SZ  11   // tamanho do botão de fechar

// Itens do menu (x, label)
static const struct { int x; const char *label; } menu_items[] = {
    {   6, "Balloon" },
    { 120, "File"    },
    { 184, "Edit"    },
    {   0, 0         }, // sentinela
};

// ----------------------------------------------------------------
// Dropdown do menu "Balloon"
// ----------------------------------------------------------------
#define DROPDOWN_X    4
#define DROPDOWN_W  148   // "Sobre o Balloon" = 15×8=120px + 14px pad cada lado

// Posições dos itens (relativas ao topo da tela)
#define DD_TOP       MENUBAR_H          // y=14: início do dropdown
#define DD_ABOUT_Y   (DD_TOP + 4)       // y=18: texto "Sobre..."
#define DD_SEP_Y     (DD_TOP + 18)      // y=32: linha separadora
#define DD_QUIT_Y    (DD_TOP + 22)      // y=36: texto "Sair"
#define DD_BOTTOM    (DD_TOP + 36)      // y=50: fim do dropdown
#define DROPDOWN_H   (DD_BOTTOM - DD_TOP)

// Áreas clicáveis dos itens (hit boxes)
#define DD_ABOUT_TOP (DD_TOP + 2)
#define DD_ABOUT_BOT (DD_TOP + 16)
#define DD_QUIT_TOP  (DD_TOP + 18)
#define DD_QUIT_BOT  (DD_BOTTOM - 2)

static int dropdown_open = 0;

// ----------------------------------------------------------------
// Cursor do mouse — desenhado em escala 2× para 640×480
// ----------------------------------------------------------------
// Máscaras lógicas de 9 bits (bit 8 = pixel esquerdo)
// O cursor real será desenhado como blocos 2×2 → tamanho final 18×26 px.

#define CUR_LOG_W  9
#define CUR_LOG_H 13
#define CUR_SCALE  2
#define CUR_W  (CUR_LOG_W * CUR_SCALE)  // 18
#define CUR_H  (CUR_LOG_H * CUR_SCALE)  // 26

// Máscara de outline (branco)
static const uint16_t cur_outline[CUR_LOG_H] = {
    0b110000000,
    0b111000000,
    0b111100000,
    0b111110000,
    0b111111000,
    0b111111100,
    0b111111110,
    0b111110000,
    0b111011000,
    0b100001100,
    0b000001100,
    0b000000110,
    0b000000000,
};

// Máscara do cursor (preto)
static const uint16_t cur_black[CUR_LOG_H] = {
    0b100000000,
    0b110000000,
    0b111000000,
    0b111100000,
    0b111110000,
    0b111111000,
    0b111111100,
    0b111110000,
    0b101001000,
    0b000000100,
    0b000000100,
    0b000000010,
    0b000000000,
};

static uint8_t cur_save[CUR_W * CUR_H]; // fundo salvo atrás do cursor (18×26 bytes)
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
            // Bloco 2×2 para escala 2×
            vga12h_pixel(x + col*2,     y + row*2,     c);
            vga12h_pixel(x + col*2 + 1, y + row*2,     c);
            vga12h_pixel(x + col*2,     y + row*2 + 1, c);
            vga12h_pixel(x + col*2 + 1, y + row*2 + 1, c);
        }
    }
}

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
// Barra de menus
// ----------------------------------------------------------------
static void draw_menubar(void) {
    vga12h_rect(0, 0, VGA12_W, MENUBAR_H - 1, C_WHITE);
    vga12h_hline(0, MENUBAR_H - 1, VGA12_W, C_BLACK);

    for (int i = 0; menu_items[i].label; i++) {
        uint8_t fg = C_BLACK, bg = C_WHITE;
        if (i == 0 && dropdown_open) {
            int iw = vga12h_strpx(menu_items[i].label) + 4;
            vga12h_rect(menu_items[i].x - 2, 0, iw, MENUBAR_H - 1, C_BLACK);
            fg = C_WHITE; bg = C_BLACK;
        }
        vga12h_string(menu_items[i].x, 3, menu_items[i].label, fg, bg);
    }

    const char *ver = "Balloon v0.1";
    int vx = VGA12_W - vga12h_strpx(ver) - 6;
    vga12h_string(vx, 3, ver, C_DGRAY, C_WHITE);
}

// ----------------------------------------------------------------
// Conteúdo da janela "Sobre o Balloon"
// ----------------------------------------------------------------
static void draw_about_content(int x, int y, int w, int h) {
    (void)h;
    const char *name = "Balloon";
    int nx = x + (w - vga12h_strpx(name)) / 2;
    vga12h_string(nx, y + 6, name, C_BLACK, C_WHITE);
    vga12h_hline(x + 6, y + 18, w - 12, C_DGRAY);
    const char *lines[] = {
        "FreeRootSD/OS v0.5",
        "Interface Balloon v0.1",
        "Modo VGA 12h  640x480",
        0
    };
    for (int i = 0; lines[i]; i++)
        vga12h_string(x + 8, y + 26 + i * 12, lines[i], C_DGRAY, C_WHITE);
}

// ----------------------------------------------------------------
// Dropdown do menu "Balloon"
// ----------------------------------------------------------------
static void draw_dropdown(void) {
    if (!dropdown_open) return;
    vga12h_rect(DROPDOWN_X, DD_TOP, DROPDOWN_W, DROPDOWN_H, C_WHITE);
    vga12h_border(DROPDOWN_X, DD_TOP, DROPDOWN_W, DROPDOWN_H, C_BLACK);
    vga12h_string(DROPDOWN_X + 8, DD_ABOUT_Y, "Sobre o Balloon", C_BLACK, C_WHITE);
    vga12h_hline(DROPDOWN_X + 4, DD_SEP_Y, DROPDOWN_W - 8, C_DGRAY);
    vga12h_string(DROPDOWN_X + 8, DD_QUIT_Y, "Sair", C_BLACK, C_WHITE);
}

// ----------------------------------------------------------------
// Desktop
// ----------------------------------------------------------------
static void draw_desktop(void) {
    vga12h_rect(0, MENUBAR_H, VGA12_W, VGA12_H - MENUBAR_H, C_LGRAY);
}

// ----------------------------------------------------------------
// Barra de título estilo Mac (listras alternadas)
// ----------------------------------------------------------------
static void draw_titlebar(int x, int y, int w, int active) {
    for (int row = 0; row < TITLEBAR_H; row++) {
        uint8_t c = (active && (row & 1)) ? C_DGRAY : C_LGRAY;
        vga12h_hline(x + 1, y + row, w - 2, c);
    }
}

// ----------------------------------------------------------------
// Botão de fechar (close box) — 11×11 px
// ----------------------------------------------------------------
static void draw_closebox(int x, int y) {
    vga12h_rect(x, y, CLOSEBOX_SZ, CLOSEBOX_SZ, C_LGRAY);
    vga12h_border(x, y, CLOSEBOX_SZ, CLOSEBOX_SZ, C_BLACK);
    // X interno (5×5 centrado em 11×11, offset 3,3)
    vga12h_pixel(x+3, y+3, C_BLACK); vga12h_pixel(x+7, y+3, C_BLACK);
    vga12h_pixel(x+4, y+4, C_BLACK); vga12h_pixel(x+6, y+4, C_BLACK);
    vga12h_pixel(x+5, y+5, C_BLACK);
    vga12h_pixel(x+4, y+6, C_BLACK); vga12h_pixel(x+6, y+6, C_BLACK);
    vga12h_pixel(x+3, y+7, C_BLACK); vga12h_pixel(x+7, y+7, C_BLACK);
}

// ----------------------------------------------------------------
// Janela completa
// ----------------------------------------------------------------
static void draw_window(int i) {
    Window *w = &windows[i];
    if (!w->active) return;

    int x = w->x, y = w->y, ww = w->w, wh = w->h;

    // Sombra simples (deslocamento 4px)
    vga12h_rect(x + 4, y + 4, ww, wh, C_DGRAY);

    // Fundo da área de conteúdo
    vga12h_rect(x + 1, y + TITLEBAR_H + 1, ww - 2, wh - TITLEBAR_H - 2, C_WHITE);

    // Barra de título listrada
    draw_titlebar(x, y, ww, 1);

    // Close box (margem vertical centralizada na titlebar)
    int cb_y = y + (TITLEBAR_H - CLOSEBOX_SZ) / 2;
    draw_closebox(x + 4, cb_y);

    // Título centralizado na titlebar
    int tx = x + (ww - vga12h_strpx(w->title)) / 2;
    int ty = y + (TITLEBAR_H - 8) / 2;
    int tw = vga12h_strpx(w->title);
    if (tw > 0) {
        vga12h_rect(tx - 1, ty - 1, tw + 2, 10, C_LGRAY);
        vga12h_string(tx, ty, w->title, C_BLACK, C_LGRAY);
    }

    // Borda preta
    vga12h_border(x, y, ww, wh, C_BLACK);
    // Linha separadora embaixo da titlebar
    vga12h_hline(x + 1, y + TITLEBAR_H, ww - 2, C_BLACK);

    // Callback de conteúdo
    if (w->draw_fn) {
        int cx = x + 1;
        int cy = y + TITLEBAR_H + 1;
        int cw = ww - 2;
        int ch = wh - TITLEBAR_H - 2;
        w->draw_fn(cx, cy, cw, ch);
    }
}

// ----------------------------------------------------------------
// Aguarda VBlank (porta 0x3DA bit 3)
// ----------------------------------------------------------------
static inline void wait_vblank(void) {
    while ( inb(0x3DA) & 0x08) {}
    while (!(inb(0x3DA) & 0x08)) {}
}

// ----------------------------------------------------------------
// Redesenha tudo (sincronizado com VBlank)
// ----------------------------------------------------------------
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
    num_windows   = 0;
    dropdown_open = 0;
    mouse_init();
    balloon_redraw();
}

BalloonWin balloon_open(const char *title, int x, int y, int w, int h,
                        BalloonDraw draw_fn) {
    for (int i = 0; i < BALLOON_MAX_WINDOWS; i++) {
        if (!windows[i].active) {
            windows[i].active  = 1;
            windows[i].x      = x;
            windows[i].y      = y;
            windows[i].w      = w;
            windows[i].h      = h;
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

// ----------------------------------------------------------------
// Loop principal de eventos
// ----------------------------------------------------------------
void balloon_run(void) {
    int drag_id  = -1;
    int drag_ox  = 0, drag_oy = 0;
    int btn_prev = 0;

    while (1) {
        // ---- Teclado ----
        while (keyboard_available()) {
            int key = getchar_nonblock();
            if (key == 27) { // Escape
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

        if (!mouse_moved()) {
            asm volatile ("hlt");
            continue;
        }

        int mx_ = mouse_x();
        int my_ = mouse_y();
        int btn = mouse_buttons();

        // ---- Borda de descida do botão esquerdo ----
        if ((btn & MOUSE_BTN_LEFT) && !(btn_prev & MOUSE_BTN_LEFT)) {

            if (hit_menu_balloon(mx_, my_)) {
                dropdown_open = !dropdown_open;
                balloon_redraw();
                goto done_click;
            }

            if (dropdown_open) {
                if (hit_dd_quit(mx_, my_)) {
                    dropdown_open = 0;
                    return;
                }
                if (hit_dd_about(mx_, my_)) {
                    dropdown_open = 0;
                    int already = 0;
                    for (int i = 0; i < BALLOON_MAX_WINDOWS; i++) {
                        if (windows[i].active) {
                            const char *t   = windows[i].title;
                            const char *ref = "Sobre o Balloon";
                            int eq = 1;
                            for (int k = 0; ref[k] || t[k]; k++)
                                if (t[k] != ref[k]) { eq = 0; break; }
                            if (eq) { already = 1; break; }
                        }
                    }
                    if (!already) {
                        int aw = 280, ah = 120;
                        int ax = (VGA12_W - aw) / 2;
                        int ay = (VGA12_H - ah) / 2;
                        balloon_open("Sobre o Balloon", ax, ay, aw, ah,
                            draw_about_content);
                    }
                    balloon_redraw();
                    goto done_click;
                }
                if (!hit_dd_area(mx_, my_)) {
                    dropdown_open = 0;
                    balloon_redraw();
                }
                goto done_click;
            }

            for (int i = BALLOON_MAX_WINDOWS - 1; i >= 0; i--) {
                if (hit_closebox(i, mx_, my_)) {
                    balloon_close(i);
                    goto done_click;
                }
            }
            for (int i = BALLOON_MAX_WINDOWS - 1; i >= 0; i--) {
                if (hit_titlebar(i, mx_, my_)) {
                    drag_id = i;
                    drag_ox = mx_ - windows[i].x;
                    drag_oy = my_ - windows[i].y;
                    goto done_click;
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

        // ---- Apenas move o cursor ----
        if (drag_id < 0) {
            cursor_erase();
            cursor_draw(mx_, my_);
        }

        btn_prev = btn;
    }
}
