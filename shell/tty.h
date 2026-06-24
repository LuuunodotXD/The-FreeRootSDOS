#ifndef TTY_H
#define TTY_H

#include <stdint.h>

#define TTY_MAX       16    // 0x0-0xF
#define TTY_COLS      80
#define TTY_ROWS      25
#define TTY_HIST_MAX  10
#define TTY_HIST_LEN  256

// Estado de um terminal
typedef struct {
    uint16_t buf[TTY_COLS * TTY_ROWS]; // buffer VGA
    int      cursor_x, cursor_y;
    uint8_t  fg, bg;
    int      active;        // 1 = pode ser aberto
    int      killed;        // 1 = kill permanente
    int      started;       // 1 = já foi iniciado (run só executa uma vez)
    char     drive;         // drive ativo ('A' ou 'H')
    uint8_t  cwd_disk;      // diretório corrente no A:
    uint8_t  cwd_heap;      // diretório corrente no H:
    int      show_time;
    int      show_drive;
    char     run_cmd[256];  // comando a executar no primeiro start
    char     hist[TTY_HIST_MAX][TTY_HIST_LEN];
    int      hist_count;
    int      hist_head;
} tty_t;

// Inicializa todos os terminais e carrega A:.VAR/tty.var
void    tty_init(void);

// Troca para terminal N. Salva estado atual, restaura N.
// Retorna 0 ok, -1 nao existe/killed
int     tty_switch(int n);

// Terminal corrente
int     tty_current(void);
tty_t  *tty_get(int n);
tty_t  *tty_active(void);  // retorna o terminal ativo

// Salva o buffer VGA atual para o terminal corrente
void    tty_save(void);

// Restaura o buffer VGA do terminal N para a tela
void    tty_restore(int n);

// Executa o run_cmd do terminal n (chamado na primeira entrada)
// Retorna 1 se executou, 0 se nao tem comando ou já executou
int     tty_run_init(int n);

// Encerra o terminal corrente (exit) — vai para tty 0
void    tty_exit(void);

// Teclas Alt+Fn
#define KEY_ALT_F1   0xA0
#define KEY_ALT_F2   0xA1
#define KEY_ALT_F3   0xA2
#define KEY_ALT_F4   0xA3
#define KEY_ALT_F5   0xA4
#define KEY_ALT_F6   0xA5
#define KEY_ALT_F7   0xA6
#define KEY_ALT_F8   0xA7
#define KEY_CTRL_ALT_F1  0xB0
#define KEY_CTRL_ALT_F2  0xB1
#define KEY_CTRL_ALT_F3  0xB2
#define KEY_CTRL_ALT_F4  0xB3

#endif
