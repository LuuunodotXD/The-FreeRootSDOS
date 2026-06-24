// shell.c - FreeRootSDOS shell principal
#include "terminal.h"
#include "keyboard.h"
#include "shell.h"
#include "io.h"
#include "idt.h"
#include "kmalloc.h"
#include "fs.h"
#include "fs_disk.h"
#include "programs.h"
#include "tty.h"
#include "env.h"
#include "netdev.h"
#include "net.h"
#include "arp.h"
#include "ip.h"
#include "icmp.h"
#include "dns.h"
#include "tcp.h"
#include "bmp.h"
#include <stddef.h>

// Protótipo para parse_and_execute (necessário para run_script)
void parse_and_execute(char *line);

// ----------------------------------------------------------------
// Utilitários de string
// ----------------------------------------------------------------
static char to_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

static int strcmpi(const char *a, const char *b) {
    while (*a && *b && to_lower(*a) == to_lower(*b)) { a++; b++; }
    return to_lower(*a) - to_lower(*b);
}

static const char *strchr(const char *s, int c) {
    while (*s && *s != (char)c) s++;
    return (*s == (char)c) ? s : 0;
}

static const char *startswith(const char *s, const char *p) {
    while (*p) if (to_lower(*s++) != to_lower(*p++)) return 0;
    return s;
}

static int hexdigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void print_dec2(unsigned int n) {
    terminal_putchar('0' + (n / 10) % 10);
    terminal_putchar('0' + (n % 10));
}

static void print_uint(uint32_t n) {
    char buf[12]; int i = 0;
    if (!n) { terminal_putchar('0'); return; }
    while (n) { buf[i++] = '0' + n % 10; n /= 10; }
    while (i--) terminal_putchar(buf[i]);
}

static void print_hex_byte(uint8_t b) {
    const char *h = "0123456789ABCDEF";
    terminal_putchar(h[b >> 4]);
    terminal_putchar(h[b & 0xF]);
}

static void print_kb(uint32_t b) { print_uint(b / 1024); terminal_writestring(" KB"); }

// Divide "arquivo.ext" em name e ext separados
static void split_filename(const char *fname,
                            char *name, int nmax,
                            char *ext,  int emax) {
    // Encontra o último ponto
    int dot = -1, i = 0;
    while (fname[i]) { if (fname[i] == '.') dot = i; i++; }

    int nlen = (dot >= 0) ? dot : i;
    if (nlen >= nmax) nlen = nmax - 1;
    for (int j = 0; j < nlen; j++) name[j] = fname[j];
    name[nlen] = 0;

    if (dot >= 0) {
        int elen = 0;
        for (int j = dot + 1; fname[j] && elen < emax - 1; j++)
            ext[elen++] = fname[j];
        ext[elen] = 0;
    } else {
        ext[0] = 0;
    }
}

// Buffer de download (32 KB em BSS)
static uint8_t wget_body[32768];

// ----------------------------------------------------------------
// Expansão de variáveis de ambiente
// ----------------------------------------------------------------
static void expand_variables(const char *src, char *dst, int dst_len) {
    env_expand(src, dst, dst_len);
}

// ----------------------------------------------------------------
// Histórico de comandos
// ----------------------------------------------------------------
#define HIST_MAX 10
#define HIST_LEN 256
static char hist[HIST_MAX][HIST_LEN];
static int  hist_count = 0, hist_head = 0;

static void hist_push(const char *cmd) {
    if (!cmd[0]) return;
    if (hist_count > 0) {
        int last = (hist_head - 1 + HIST_MAX) % HIST_MAX;
        const char *a = hist[last], *b = cmd;
        int eq = 1;
        while (*a || *b) { if (*a++ != *b++) { eq = 0; break; } }
        if (eq) return;
    }
    int i = 0;
    while (cmd[i] && i < HIST_LEN - 1) { hist[hist_head][i] = cmd[i]; i++; }
    hist[hist_head][i] = '\0';
    hist_head = (hist_head + 1) % HIST_MAX;
    if (hist_count < HIST_MAX) hist_count++;
}

static const char *hist_get(int idx) {
    if (idx < 0 || idx >= hist_count) return 0;
    int slot = (hist_head - 1 - idx + HIST_MAX * 2) % HIST_MAX;
    return hist[slot];
}

// ----------------------------------------------------------------
// Drive ativo: 'H' = heap RAM, 'A' = disco persistente
// ----------------------------------------------------------------
static char active_drive = 'A';
static int  tty_switch_pending = -1; // -1 = nenhum

static int on_disk(void) { return active_drive == 'A'; }

static uint8_t     drv_cwd(void)      { return on_disk() ? fsd_cwd()      : fs_cwd(); }
static const char *drv_cwd_name(void) { return on_disk() ? fsd_cwd_name() : fs_cwd_name(); }
static void drv_cwd_path(char *buf, int maxlen) {
    if (on_disk()) fsd_cwd_path(buf, maxlen);
    else           fs_cwd_path(buf, maxlen);
}

static int drv_cd(const char *n)      { return on_disk() ? fsd_cd(n)      : fs_cd(n); }
static int drv_mkdir(const char *n)   { return on_disk() ? fsd_mkdir(n)   : fs_mkdir(n); }
static int drv_write(const char *name, const char *ext, const char *c, uint32_t s) {
    return on_disk() ? fsd_write(name, ext, c, s) : fs_write(name, ext, c, s);
}
static int drv_write_in(uint8_t dir, const char *name, const char *ext, const char *c, uint32_t s) {
    return on_disk() ? fsd_write_in(dir, name, ext, c, s) : fs_write_in(dir, name, ext, c, s);
}
static const char *drv_read(const char *name, const char *ext) {
    return on_disk() ? fsd_read(name, ext) : fs_read(name, ext);
}
static int drv_delete(const char *name, const char *ext, int is_dir) {
    return on_disk() ? fsd_delete(name, ext, is_dir) : fs_delete(name, ext, is_dir);
}
static int drv_rename(const char *n, const char *e, const char *nn, const char *ne) {
    return on_disk() ? fsd_rename(n, e, nn, ne) : fs_rename(n, e, nn, ne);
}
static void drv_split(const char *in, char *name, char *ext) {
    if (on_disk()) fsd_split(in, name, ext); else fs_split(in, name, ext);
}
static void *drv_table(void)  { return on_disk() ? (void*)fsd_table() : (void*)fs_table(); }
static int   drv_max(void)    { return on_disk() ? fsd_max() : fs_max(); }
static int   drv_is_disk(void){ return on_disk(); }

// ----------------------------------------------------------------
// RTC (data/hora)
// ----------------------------------------------------------------
static uint8_t bcd_to_bin(uint8_t v) { return (v >> 4) * 10 + (v & 0xF); }
static uint8_t rtc_read(uint8_t reg) { outb(0x70, reg); return inb(0x71); }

// ----------------------------------------------------------------
// Reboot / Poweroff
// ----------------------------------------------------------------
static int show_time  = 0;
static int show_drive = 1;

void shell_init(void) {}

void reboot(void) {
    asm volatile ("cli");               // para IRQs para não chegar mais dados do mouse

    // 1. Desabilita o dispositivo auxiliar (mouse) no 8042
    while (inb(0x64) & 0x02);
    outb(0x64, 0xA7);                   // Disable Auxiliary Device

    // 2. Drena o buffer de saída do 8042 (dados pendentes do mouse)
    for (int i = 0; i < 16; i++) {
        if (!(inb(0x64) & 0x01)) break;
        inb(0x60);
    }

    // 3. Restaura o Command Byte do 8042 ao estado de POST:
    //    IRQ1 on (bit0), System flag (bit2), mouse desabilitado (bit5),
    //    scan-code translation on (bit6) — assim o próximo boot acha teclado limpo
    while (inb(0x64) & 0x02);
    outb(0x64, 0x60);                   // Write Command Byte
    while (inb(0x64) & 0x02);
    outb(0x60, 0x65);                   // 0b01100101

    // 4. Pulse na linha de reset da CPU via 8042
    while (inb(0x64) & 0x02);
    outb(0x64, 0xFE);
    while (1) asm volatile ("hlt");
}

static void poweroff(void) {
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    terminal_writestring("Poweroff nao suportado.\n");
    while (1) asm volatile ("hlt");
}

// ----------------------------------------------------------------
// Leitura de linha com edição (histórico, setas, etc.)
// ----------------------------------------------------------------
static void redraw_from(const char *buf, int from, int len, int cur) {
    for (int i = from; i < len; i++) terminal_putchar(buf[i]);
    terminal_putchar(' ');
    terminal_cursor_left(len - from + 1);
    terminal_cursor_right(cur - from);
}

static int tc_strncmpi(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        char ca = a[i], cb = b[i];
        if (ca>='a'&&ca<='z') ca-=32; if (cb>='a'&&cb<='z') cb-=32;
        if (!ca && !cb) return 0;
        if (ca != cb) return ca - cb;
    }
    return 0;
}

static void tc_lower(char *dst, const char *src) {
    int i = 0;
    while (src[i] && i < 31) {
        char c = src[i];
        if (c>='A'&&c<='Z') c+=32;
        dst[i] = c; i++;
    }
    dst[i] = 0;
}

static void tab_complete(char *buf, int *lenp, int *curp, int maxlen) {
    // Palavra sendo digitada: do último espaço até o cursor
    int ws = *curp;
    while (ws > 0 && buf[ws-1] != ' ') ws--;
    int plen = *curp - ws;
    if (plen >= 31) return;

    char prefix[32]; int pi = 0;
    for (int i = ws; i < *curp; i++) prefix[pi++] = buf[i];
    prefix[pi] = 0;
    int is_cmd = (ws == 0);

    // Lista de comandos built-in
    static const char *cmds[] = {
        "dir","cd","md","del","rename","copy","move","write","cat","append",
        "clear","cat","help","time","date","reboot","format","env","set","unset",
        "drive","balloon","pwd","beep","view","wget","ping","arping","resolve",
        "ifconfig","arp","ntp","edit","poweroff", 0
    };

    char matches[16][16]; int nm = 0;

    // Completa comandos se for a primeira palavra
    if (is_cmd) {
        for (int i = 0; cmds[i] && nm < 16; i++)
            if (tc_strncmpi(cmds[i], prefix, plen) == 0) {
                tc_lower(matches[nm], cmds[i]); nm++;
            }
    }

    // Completa arquivos/diretórios do cwd
    uint8_t cwd_idx = drv_cwd();
    if (on_disk()) {
        fsd_entry_t *t = fsd_table();
        for (int i = 1; i < fsd_max() && nm < 16; i++) {
            if (t[i].type == FSD_FREE || t[i].parent != cwd_idx) continue;
            char fname[16] = {0}; int fi = 0;
            for (int j=0;t[i].name[j]&&fi<8;j++) fname[fi++]=t[i].name[j];
            if (t[i].ext[0] && t[i].type == FSD_FILE)
                { fname[fi++]='.'; for(int j=0;t[i].ext[j]&&fi<12;j++) fname[fi++]=t[i].ext[j]; }
            fname[fi] = 0;
            if (tc_strncmpi(fname, prefix, plen) == 0) {
                tc_lower(matches[nm], fname); nm++;
            }
        }
    } else {
        fs_entry_t *t = fs_table();
        for (int i = 1; i < fs_max() && nm < 16; i++) {
            if (t[i].type == FS_FREE || t[i].parent != cwd_idx) continue;
            char fname[16] = {0}; int fi = 0;
            for (int j=0;t[i].name[j]&&fi<8;j++) fname[fi++]=t[i].name[j];
            if (t[i].ext[0] && t[i].type == FS_FILE)
                { fname[fi++]='.'; for(int j=0;t[i].ext[j]&&fi<12;j++) fname[fi++]=t[i].ext[j]; }
            fname[fi] = 0;
            if (tc_strncmpi(fname, prefix, plen) == 0) {
                tc_lower(matches[nm], fname); nm++;
            }
        }
    }

    if (nm == 0) return;

    if (nm == 1) {
        // Único match: completa in-place
        const char *rest = matches[0] + plen;
        int rlen = 0; while (rest[rlen]) rlen++;
        int add  = rlen + (is_cmd ? 1 : 0);
        if (*lenp + add >= maxlen) return;
        // Abre espaço no buffer
        for (int i = *lenp; i >= *curp; i--) buf[i + add] = buf[i];
        for (int i = 0; i < rlen; i++) buf[*curp + i] = rest[i];
        if (is_cmd) buf[*curp + rlen] = ' ';
        *lenp += add; *curp += add; buf[*lenp] = 0;
        // Exibe os chars inseridos
        for (int i = *curp - add; i < *curp; i++) terminal_putchar(buf[i]);
        // Re-exibe o restante da linha após o cursor
        for (int i = *curp; i < *lenp; i++) terminal_putchar(buf[i]);
        terminal_cursor_left(*lenp - *curp);
    } else {
        // Múltiplos matches: exibe na linha de baixo e reimprime o buffer
        terminal_putchar('\n');
        for (int i = 0; i < nm; i++) {
            terminal_writestring(matches[i]);
            terminal_putchar(' ');
        }
        terminal_putchar('\n');
        // Reimprime prompt simplificado (só "> ")
        char pathbuf[64];
        drv_cwd_path(pathbuf, sizeof(pathbuf));
        if (show_drive) { terminal_putchar(active_drive); terminal_putchar(':'); }
        terminal_writestring(pathbuf[0] ? pathbuf : "/");
        terminal_writestring("> ");
        // Reimprime buffer atual e reposiciona cursor
        for (int i = 0; i < *lenp; i++) terminal_putchar(buf[i]);
        terminal_cursor_left(*lenp - *curp);
    }
}

static void readline(char *buf, int maxlen) {
    int len = 0, cur = 0, hist_idx = -1;
    char saved[HIST_LEN]; saved[0] = '\0';
    while (1) {
        int k = getchar();

        if (k == '\n') {
            buf[len] = '\0';
            terminal_putchar('\n');
            return;
        }
        if (k == '\b') {
            if (cur == 0) continue;
            for (int i = cur - 1; i < len - 1; i++) buf[i] = buf[i + 1];
            len--; cur--;
            terminal_cursor_left(1);
            redraw_from(buf, cur, len, cur);
            continue;
        }
	if (k == '\t') {
	    tab_complete(buf, &len, &cur, maxlen);
	    hist_idx = -1;
	    continue;
	}
        if (k == KEY_DEL) {
            if (cur == len) continue;
            for (int i = cur; i < len - 1; i++) buf[i] = buf[i + 1];
            len--;
            redraw_from(buf, cur, len, cur);
            continue;
        }
        if (k == KEY_LEFT) { if (cur > 0) { cur--; terminal_cursor_left(1); } continue; }
        if (k == KEY_RIGHT){ if (cur < len) { cur++; terminal_cursor_right(1); } continue; }
        if (k == KEY_HOME) { terminal_cursor_left(cur); cur = 0; continue; }
        if (k == KEY_END)  { terminal_cursor_right(len - cur); cur = len; continue; }
        // Alt+F1-F8: troca terminal
        if (k >= KEY_ALT_F1 && k <= KEY_ALT_F8) {
            terminal_cursor_left(cur);
            for (int i = 0; i < len; i++) terminal_putchar(' ');
            terminal_cursor_left(len);
            tty_switch_pending = k - KEY_ALT_F1;
            buf[0] = '\0';
            return;
        }
        if (k >= KEY_CTRL_ALT_F1 && k <= KEY_CTRL_ALT_F8) {
            terminal_cursor_left(cur);
            for (int i = 0; i < len; i++) terminal_putchar(' ');
            terminal_cursor_left(len);
            tty_switch_pending = k - KEY_CTRL_ALT_F1 + 8;
            buf[0] = '\0';
            return;
        }
        if (k == KEY_UP || k == KEY_DOWN) {
            if (k == KEY_UP && hist_idx + 1 >= hist_count) continue;
            if (k == KEY_DOWN && hist_idx == -1) continue;
            if (k == KEY_UP && hist_idx == -1) {
                int i = 0;
                while (i < len) { saved[i] = buf[i]; i++; }
                saved[i] = '\0';
            }
            hist_idx += (k == KEY_UP) ? 1 : -1;
            const char *entry = (hist_idx == -1) ? saved : hist_get(hist_idx);
            terminal_cursor_left(cur);
            for (int i = 0; i < len; i++) terminal_putchar(' ');
            terminal_cursor_left(len);
            len = 0;
            while (entry[len] && len < maxlen - 1) { buf[len] = entry[len]; len++; }
            buf[len] = '\0'; cur = len;
            terminal_writestring(buf);
            continue;
        }
        if (k >= ' ' && k <= '~' && len < maxlen - 1) {
            for (int i = len; i > cur; i--) buf[i] = buf[i - 1];
            buf[cur] = (char)k;
            len++; cur++;
            for (int i = cur - 1; i < len; i++) terminal_putchar(buf[i]);
            terminal_cursor_left(len - cur);
        }
    }
}

// ----------------------------------------------------------------
// Funções auxiliares para dir com opções -a -s
// ----------------------------------------------------------------
static int is_hidden(const char *name) {
    return (name[0] == '.');
}

static void print_indent(int depth) {
    for (int i = 0; i < depth; i++) terminal_writestring("  ");
}

static void print_tree(uint8_t parent, int depth, int show_hidden) {
    if (depth > 16) return;
    if (on_disk()) {
        fsd_entry_t *t = fsd_table();
        for (int i = 1; i < fsd_max(); i++) {
            if (t[i].type != FSD_DIR) continue;
            if (t[i].parent != parent) continue;
            if (!show_hidden && is_hidden(t[i].name)) continue;
            print_indent(depth);
            terminal_writestring(t[i].name);
            terminal_writestring("/\n");
            print_tree(i, depth + 1, show_hidden);
        }
        for (int i = 1; i < fsd_max(); i++) {
            if (t[i].type != FSD_FILE) continue;
            if (t[i].parent != parent) continue;
            if (!show_hidden && is_hidden(t[i].name)) continue;
            print_indent(depth);
            terminal_writestring(t[i].name);
            if (t[i].ext[0]) { terminal_putchar('.'); terminal_writestring(t[i].ext); }
            terminal_putchar('\n');
        }
    } else {
        fs_entry_t *t = fs_table();
        for (int i = 1; i < fs_max(); i++) {
            if (t[i].type != FS_DIR) continue;
            if (t[i].parent != parent) continue;
            if (!show_hidden && is_hidden(t[i].name)) continue;
            print_indent(depth);
            terminal_writestring(t[i].name);
            terminal_writestring("/\n");
            print_tree(i, depth + 1, show_hidden);
        }
        for (int i = 1; i < fs_max(); i++) {
            if (t[i].type != FS_FILE) continue;
            if (t[i].parent != parent) continue;
            if (!show_hidden && is_hidden(t[i].name)) continue;
            print_indent(depth);
            terminal_writestring(t[i].name);
            if (t[i].ext[0]) { terminal_putchar('.'); terminal_writestring(t[i].ext); }
            terminal_putchar('\n');
        }
    }
}

static void list_flat(uint8_t parent, int show_hidden) {
    int count = 0;
    if (on_disk()) {
        fsd_entry_t *t = fsd_table();
        for (int i = 1; i < fsd_max(); i++) {
            if (t[i].type != FSD_DIR) continue;
            if (t[i].parent != (uint8_t)parent) continue;
            if (!show_hidden && is_hidden(t[i].name)) continue;
            terminal_writestring(t[i].name);
            terminal_writestring("/\n");
            count++;
        }
        for (int i = 1; i < fsd_max(); i++) {
            if (t[i].type != FSD_FILE) continue;
            if (t[i].parent != (uint8_t)parent) continue;
            if (!show_hidden && is_hidden(t[i].name)) continue;
            terminal_writestring(t[i].name);
            if (t[i].ext[0]) { terminal_putchar('.'); terminal_writestring(t[i].ext); }
            terminal_writestring("  ");
            print_uint(t[i].size);
            terminal_writestring(" bytes\n");
            count++;
        }
    } else {
        fs_entry_t *t = fs_table();
        for (int i = 1; i < fs_max(); i++) {
            if (t[i].type != FS_DIR) continue;
            if (t[i].parent != (uint8_t)parent) continue;
            if (!show_hidden && is_hidden(t[i].name)) continue;
            terminal_writestring(t[i].name);
            terminal_writestring("/\n");
            count++;
        }
        for (int i = 1; i < fs_max(); i++) {
            if (t[i].type != FS_FILE) continue;
            if (t[i].parent != (uint8_t)parent) continue;
            if (!show_hidden && is_hidden(t[i].name)) continue;
            terminal_writestring(t[i].name);
            if (t[i].ext[0]) { terminal_putchar('.'); terminal_writestring(t[i].ext); }
            terminal_writestring("  ");
            print_uint(t[i].size);
            terminal_writestring(" bytes\n");
            count++;
        }
    }
    if (count == 0) terminal_writestring("Vazio.\n");
}

// Comando dir com opções estilo Unix: -a (ocultos), -s (árvore)
static void cmd_dir(const char *arg) {
    int show_hidden = 0;
    int tree_mode = 0;
    const char *dirname = NULL;

    if (arg) {
        char buf[256];
        int i = 0;
        while (arg[i] && i < 255) { buf[i] = arg[i]; i++; }
        buf[i] = '\0';
        char *token = buf;
        while (token && *token) {
            char *next = token;
            while (*next && *next != ' ') next++;
            if (*next) { *next = '\0'; next++; } else next = NULL;
            if (token[0] == '-') {
                for (int j = 1; token[j]; j++) {
                    if (token[j] == 'a' || token[j] == 'A') show_hidden = 1;
                    if (token[j] == 's' || token[j] == 'S') tree_mode = 1;
                }
            } else {
                dirname = token;
            }
            token = next;
        }
    }

    uint8_t show_dir = drv_cwd();
    if (dirname) {
        if (on_disk()) {
            fsd_entry_t *t = fsd_table();
            int idx = -1;
            for (int i = 1; i < fsd_max(); i++) {
                if (t[i].type != FSD_DIR) continue;
                if (strcmpi(t[i].name, dirname) == 0) { idx = i; break; }
            }
            if (idx < 0) {
                terminal_writestring("Diretorio nao encontrado.\n");
                return;
            }
            show_dir = (uint8_t)idx;
        } else {
            fs_entry_t *t = fs_table();
            int idx = -1;
            for (int i = 1; i < fs_max(); i++) {
                if (t[i].type != FS_DIR) continue;
                if (strcmpi(t[i].name, dirname) == 0) { idx = i; break; }
            }
            if (idx < 0) {
                terminal_writestring("Diretorio nao encontrado.\n");
                return;
            }
            show_dir = (uint8_t)idx;
        }
    }

    if (tree_mode) {
        const char *root_name = (dirname ? dirname : drv_cwd_name());
        if (root_name && root_name[0]) {
            terminal_writestring(root_name);
            terminal_writestring(":\n");
        } else {
            terminal_writestring("Raiz:\n");
        }
        print_tree(show_dir, 1, show_hidden);
    } else {
        if (dirname) {
            terminal_writestring(dirname);
            terminal_writestring(":\n");
        }
        list_flat(show_dir, show_hidden);
    }
}

// ----------------------------------------------------------------
// Comandos do sistema de arquivos (write, cat, del, rename, append, copy, move, format)
// ----------------------------------------------------------------
static void cmd_write(const char *arg) {
    char name[9], ext[4];
    int i = 0;
    while (arg[i] && arg[i] != ' ') i++;
    char fname[16];
    for (int j = 0; j < i && j < 15; j++) fname[j] = arg[j];
    fname[i] = '\0';
    if (!fname[0]) { terminal_writestring("Uso: write <arquivo> [texto]\n"); return; }
    drv_split(fname, name, ext);
    const char *content = arg[i] ? arg + i + 1 : 0;
    uint32_t size = 0;
    if (content) while (content[size]) size++;
    int ret = drv_write(name, ext, content, size);
    if (ret == 0) terminal_writestring("Arquivo salvo.\n");
    else if (ret == -2) terminal_writestring("Nome invalido.\n");
    else terminal_writestring("Sem espaco.\n");
}

static void cmd_cat(const char *arg) {
    char name[9], ext[4];
    drv_split(arg, name, ext);
    const char *data = drv_read(name, ext);
    if (!data) terminal_writestring("Nao encontrado ou vazio.\n");
    else { terminal_writestring(data); terminal_putchar('\n'); }
}

static void cmd_del(const char *arg) {
    char name[9], ext[4];
    drv_split(arg, name, ext);
    if (ext[0] == '\0') {
        if (on_disk()) {
            fsd_entry_t *t = fsd_table();
            for (int i = 1; i < fsd_max(); i++) {
                if (t[i].type != FSD_DIR) continue;
                if (strcmpi(t[i].name, name) == 0) {
                    fsd_delete(name, "", 1);
                    terminal_writestring("Diretorio removido.\n");
                    return;
                }
            }
        } else {
            fs_entry_t *t = fs_table();
            for (int i = 1; i < fs_max(); i++) {
                if (t[i].type != FS_DIR) continue;
                if (strcmpi(t[i].name, name) == 0) {
                    fs_delete(name, "", 1);
                    terminal_writestring("Diretorio removido.\n");
                    return;
                }
            }
        }
    }
    if (drv_delete(name, ext, 0) == 0) terminal_writestring("Deletado.\n");
    else terminal_writestring("Nao encontrado.\n");
}

static void cmd_rename(const char *arg) {
    char on[9], oe[4], nn[9], ne[4];
    int i = 0;
    while (arg[i] && arg[i] != ' ') i++;
    if (!arg[i]) { terminal_writestring("Uso: rename <antigo> <novo>\n"); return; }
    char a[16], b[16];
    int j = 0;
    while (j < i && j < 15) { a[j] = arg[j]; j++; }
    a[j] = '\0';
    const char *rest = arg + i + 1;
    j = 0;
    while (rest[j] && j < 15) { b[j] = rest[j]; j++; }
    b[j] = '\0';
    drv_split(a, on, oe);
    drv_split(b, nn, ne);
    int ret = drv_rename(on, oe, nn, ne);
    if (ret == 0) terminal_writestring("Renomeado.\n");
    else if (ret == -1) terminal_writestring("Nao encontrado.\n");
    else terminal_writestring("Destino ja existe.\n");
}

static void cmd_append(const char *arg) {
    char name[9], ext[4]; int i = 0;
    while (arg[i] && arg[i] != ' ') i++;
    if (!arg[i]) { terminal_writestring("Uso: append <arquivo> <texto>\n"); return; }
    char fname[16]; for (int j = 0; j < i && j < 15; j++) fname[j] = arg[j]; fname[i] = '\0';
    drv_split(fname, name, ext);
    const char *add = arg + i + 1;
    if (!add[0]) { terminal_writestring("Uso: append <arquivo> <texto>\n"); return; }
    const char *cur = drv_read(name, ext);
    uint32_t cur_len = 0; if (cur) while (cur[cur_len]) cur_len++;
    uint32_t add_len = 0; while (add[add_len]) add_len++;
    uint32_t new_len = cur_len + (cur_len ? 1 : 0) + add_len;
    char *buf = (char*)kmalloc(new_len + 1);
    if (!buf) { terminal_writestring("Sem memoria.\n"); return; }
    uint32_t pos = 0;
    for (uint32_t j = 0; j < cur_len; j++) buf[pos++] = cur[j];
    if (cur_len) buf[pos++] = '\n';
    for (uint32_t j = 0; j < add_len; j++) buf[pos++] = add[j];
    buf[pos] = '\0';
    int ret = drv_write(name, ext, buf, new_len);
    kfree(buf);
    if (ret == 0) terminal_writestring("Linha adicionada.\n");
    else terminal_writestring("Erro.\n");
}

static int find_dir_idx(const char *dname, char drive) {
    if (drive == 'A') {
        fsd_entry_t *t = fsd_table();
        for (int i = 1; i < fsd_max(); i++) {
            if (t[i].type != FSD_DIR) continue;
            if (strcmpi(t[i].name, dname) == 0) return i;
        }
    } else {
        fs_entry_t *t = fs_table();
        for (int i = 1; i < fs_max(); i++) {
            if (t[i].type != FS_DIR) continue;
            if (strcmpi(t[i].name, dname) == 0) return i;
        }
    }
    return -1;
}

static void cmd_copy_or_move(const char *arg, int do_move) {
    char fname[16]; int i = 0;
    while (arg[i] && arg[i] != ' ') i++;
    for (int j = 0; j < i && j < 15; j++) fname[j] = arg[j]; fname[i] = '\0';
    if (!fname[0]) {
        terminal_writestring(do_move ? "Uso: move <arq> [destino]\n" : "Uso: copy <arq> [destino]\n");
        return;
    }
    char name[9], ext[4];
    drv_split(fname, name, ext);
    const char *data = drv_read(name, ext);
    uint32_t src_size = 0;
    if (data) while (data[src_size]) src_size++;

    const char *dest_arg = arg[i] ? arg + i + 1 : 0;
    char dest_drive = active_drive;
    const char *dest_dir_name = 0;
    uint8_t dest_dir = 0;

    if (dest_arg && dest_arg[0]) {
        if ((dest_arg[0]=='A'||dest_arg[0]=='a') && dest_arg[1]==':') {
            dest_drive = 'A'; dest_dir_name = dest_arg[2] ? dest_arg+2 : 0;
        } else if ((dest_arg[0]=='H'||dest_arg[0]=='h') && dest_arg[1]==':') {
            dest_drive = 'H'; dest_dir_name = dest_arg[2] ? dest_arg+2 : 0;
        } else {
            dest_dir_name = dest_arg;
        }
    }

    if (dest_dir_name && dest_dir_name[0]) {
        int idx = find_dir_idx(dest_dir_name, dest_drive);
        if (idx < 0) { terminal_writestring("Destino nao encontrado.\n"); return; }
        dest_dir = (uint8_t)idx;
    }

    int ret;
    if (dest_drive == 'A') ret = fsd_write_in(dest_dir, name, ext, data, src_size);
    else ret = fs_write_in(dest_dir, name, ext, data, src_size);

    if (ret != 0) { terminal_writestring("Erro ao copiar.\n"); return; }

    if (do_move) {
        drv_delete(name, ext, 0);
        terminal_writestring("Movido.\n");
    } else {
        terminal_writestring("Copiado.\n");
    }
}

static void cmd_format(const char *arg) {
    char target = active_drive;
    if (arg && (arg[0] == 'A' || arg[0] == 'a')) target = 'A';
    else if (arg && (arg[0] == 'H' || arg[0] == 'h')) target = 'H';
    if (target == 'A') { fsd_format(); terminal_writestring("A: formatado.\n"); }
    else { fs_format();  terminal_writestring("H: formatado.\n"); }
}

// ----------------------------------------------------------------
// Funções auxiliares para scripts .cha e binários .bin
// ----------------------------------------------------------------
static char *load_script(const char *name) {
    char name_buf[9], ext_buf[4];
    drv_split(name, name_buf, ext_buf);
    if (ext_buf[0] && strcmpi(ext_buf, "cha") != 0) return NULL;
    const char *data = drv_read(name_buf, "cha");
    if (!data) return NULL;
    uint32_t len = 0;
    while (data[len]) len++;
    char *copy = (char*)kmalloc(len + 1);
    if (!copy) return NULL;
    for (uint32_t i = 0; i <= len; i++) copy[i] = data[i];
    return copy;
}

static void run_script(const char *script) {
    const char *p = script;
    char line[512];
    while (*p) {
        int i = 0;
        while (*p && *p != '\n' && i < (int)sizeof(line)-1) line[i++] = *p++;
        line[i] = '\0';
        if (*p == '\n') p++;
        if (line[0] != '\0') parse_and_execute(line);
    }
}

static int execute_binary(const char *name) {
    char name_buf[9], ext_buf[4];
    drv_split(name, name_buf, ext_buf);
    if (ext_buf[0] && strcmpi(ext_buf, "bin") != 0) return 0;
    const char *data = drv_read(name_buf, "bin");
    if (!data) return 0;
    uint32_t size = 0;
    if (on_disk()) {
        fsd_entry_t *t = fsd_table();
        for (int i = 0; i < fsd_max(); i++) {
            if (t[i].type == FSD_FILE && strcmpi(t[i].name, name_buf) == 0 && strcmpi(t[i].ext, "bin") == 0) {
                size = t[i].size; break;
            }
        }
    } else {
        fs_entry_t *t = fs_table();
        for (int i = 0; i < fs_max(); i++) {
            if (t[i].type == FS_FILE && strcmpi(t[i].name, name_buf) == 0 && strcmpi(t[i].ext, "bin") == 0) {
                size = t[i].size; break;
            }
        }
    }
    if (size == 0) return 0;
    uint8_t *dest = (uint8_t*)0x20000;
    for (uint32_t i = 0; i < size; i++) dest[i] = data[i];
    terminal_writestring("Executando...\n");
    asm volatile ("call *%0" : : "r"(0x20000) : "memory");
    terminal_writestring("\nPrograma finalizado.\n");
    return 1;
}

// ----------------------------------------------------------------
// Execução de um comando individual (sem ';')
// ----------------------------------------------------------------
static int execute_command(const char *cmd) {
    if (!cmd || !cmd[0]) return 1;

    // Troca de drive
    if (strcmpi(cmd, "a:") == 0) { active_drive = 'A'; terminal_writestring("Drive A: (disco)\n"); return 1; }
    if (strcmpi(cmd, "h:") == 0) { active_drive = 'H'; terminal_writestring("Drive H: (heap/RAM)\n"); return 1; }

    // Comandos básicos
    if (strcmpi(cmd, "clear") == 0) {
        terminal_clear();
        return 1;
    }
    if (strcmpi(cmd, "reboot") == 0) { terminal_writestring("Reiniciando...\n"); reboot(); return 1; }
    if (strcmpi(cmd, "poweroff") == 0) { terminal_writestring("Desligando...\n"); poweroff(); return 1; }
    if (strcmpi(cmd, "info") == 0) {
        terminal_writestring("FreeRootSDOS v0.7 Tsar (Maior update de todos)\n");
        terminal_writestring("Drive A: disco persistente | Drive H: heap RAM\n");
        return 1;
    }
    if (strcmpi(cmd, "date") == 0) {
        uint8_t sec = bcd_to_bin(rtc_read(0x00));
        uint8_t min = bcd_to_bin(rtc_read(0x02));
        uint8_t hr  = bcd_to_bin(rtc_read(0x04));
        uint8_t day = bcd_to_bin(rtc_read(0x07));
        uint8_t mon = bcd_to_bin(rtc_read(0x08));
        uint8_t yr  = bcd_to_bin(rtc_read(0x09));
        print_dec2(day); terminal_putchar('/');
        print_dec2(mon); terminal_putchar('/');
        terminal_writestring("20");
        print_dec2(yr); terminal_writestring("  ");
        print_dec2(hr); terminal_putchar(':');
        print_dec2(min); terminal_putchar(':');
        print_dec2(sec); terminal_putchar('\n');
        return 1;
    }
    if (strcmpi(cmd, "uptime") == 0) {
        uint32_t s = timer_get_ticks() / 1000;
        terminal_writestring("Uptime: ");
        print_dec2(s / 3600); terminal_putchar(':');
        print_dec2((s % 3600) / 60); terminal_putchar(':');
        print_dec2(s % 60); terminal_putchar('\n');
        return 1;
    }
    if (startswith(cmd, "sleep ")) {
        const char *a = startswith(cmd, "sleep ");
        uint32_t ms = 0;
        while (*a >= '0' && *a <= '9') ms = ms * 10 + (*a++ - '0');
        if (!ms || *a) terminal_writestring("Uso: sleep <ms>\n");
        else { timer_sleep(ms); terminal_writestring("Pronto.\n"); }
        return 1;
    }
    if (strcmpi(cmd, "time") == 0) {
        show_time ^= 1;
        terminal_writestring(show_time ? "Hora ativada.\n" : "Hora desativada.\n");
        return 1;
    }
    if (strcmpi(cmd, "drive") == 0) {
        show_drive ^= 1;
        terminal_writestring(show_drive ? "Unidade ativada.\n" : "Unidade desativada.\n");
        return 1;
    }
    // log
    if (startswith(cmd, "log:")) {
        const char *arg = cmd + 4;
        int cor = hexdigit(arg[0]);
        if (cor >= 0) {
            const char *text = arg + 1;
            while (*text == ' ') text++;
            uint8_t old = terminal_get_fg();
            terminal_set_fg((uint8_t)cor);
            terminal_writestring(text);
            terminal_putchar('\n');
            terminal_set_fg(old);
        } else {
            terminal_writestring(arg);
            terminal_putchar('\n');
        }
        return 1;
    }
    if (startswith(cmd, "log ")) {
        terminal_writestring(startswith(cmd, "log "));
        terminal_putchar('\n');
        return 1;
    }
    // cores
    if (startswith(cmd, "color ")) {
        const char *a = startswith(cmd, "color ");
        int v = hexdigit(a[0]);
        if (v < 0 || a[1]) terminal_writestring("Uso: color <0-F>\n");
        else { terminal_set_fg((uint8_t)v); terminal_writestring("Cor alterada.\n"); }
        return 1;
    }
    if (startswith(cmd, "bgcolor ")) {
        const char *a = startswith(cmd, "bgcolor ");
        int v = hexdigit(a[0]);
        if (v < 0 || a[1]) terminal_writestring("Uso: bgcolor <0-F>\n");
	else { terminal_set_bg((uint8_t)v); terminal_clear(); terminal_writestring("Cor alterada.\n"); }
        return 1;
    }
    if (strcmpi(cmd, "meminfo") == 0) {
        uint32_t total = 64u * 1024u, free = kmalloc_free();
        terminal_writestring("Heap total: "); print_kb(total); terminal_putchar('\n');
        terminal_writestring("Heap usado: "); print_kb(total - free); terminal_putchar('\n');
        terminal_writestring("Heap livre: "); print_kb(free); terminal_putchar('\n');
        uint32_t disk_total = FSD_DATA_SECTORS * 512u;
        uint32_t disk_used = 0;
        fsd_entry_t *t = fsd_table();
        for (int i = 1; i < fsd_max(); i++) if (t[i].type == FSD_FILE) disk_used += t[i].num_secs * 512u;
        terminal_writestring("Disco total: "); print_kb(disk_total); terminal_putchar('\n');
        terminal_writestring("Disco usado: "); print_kb(disk_used);  terminal_putchar('\n');
        terminal_writestring("Disco livre: "); print_kb(disk_total - disk_used); terminal_putchar('\n');
        return 1;
    }

    // Comando clássicos com opções estilo Unix
    if (strcmpi(cmd, "dir") == 0)       { cmd_dir(0); return 1; }
    if (startswith(cmd, "dir "))         { cmd_dir(startswith(cmd, "dir ")); return 1; }
    if (startswith(cmd, "dir -"))        { cmd_dir(startswith(cmd, "dir ")); return 1; }
    if (startswith(cmd, "md ")) {
        const char *a = startswith(cmd, "md ");
        int r = drv_mkdir(a);
        if (r >= 0) terminal_writestring("Diretorio criado.\n");
        else if (r == -3) terminal_writestring("Ja existe.\n");
        else if (r == -2) terminal_writestring("Nome invalido.\n");
        else terminal_writestring("Sem espaco.\n");
        return 1;
    }
    if (strcmpi(cmd, "pwd") == 0) {
        char pb[64];
        drv_cwd_path(pb, sizeof(pb));
        terminal_writestring(pb[0] ? pb : "/");
        terminal_putchar('\n');
        return 1;
    }
    if (startswith(cmd, "view ")) {
        const char *fname = startswith(cmd, "view ");
        char nm[9]={0}, ex[4]={0};
        split_filename(fname, nm, 9, ex, 4);
        bmp_view(nm, ex);
        return 1;
    }
    if (startswith(cmd, "cd ")) { drv_cd(startswith(cmd, "cd ")); return 1; }
    if (strcmpi(cmd, "cd..") == 0 || strcmpi(cmd, "cd ..") == 0) { drv_cd(".."); return 1; }
    if (startswith(cmd, "write ")) { cmd_write(startswith(cmd, "write ")); return 1; }
    if (startswith(cmd, "cat "))   { cmd_cat(startswith(cmd, "cat ")); return 1; }
    if (startswith(cmd, "del "))   { cmd_del(startswith(cmd, "del ")); return 1; }
    if (startswith(cmd, "rename ")){ cmd_rename(startswith(cmd, "rename ")); return 1; }
    if (startswith(cmd, "append ")) { cmd_append(startswith(cmd, "append ")); return 1; }
    if (startswith(cmd, "copy "))   { cmd_copy_or_move(startswith(cmd, "copy "),  0); return 1; }
    if (startswith(cmd, "move "))   { cmd_copy_or_move(startswith(cmd, "move "),  1); return 1; }
    if (strcmpi(cmd, "format") == 0 || startswith(cmd, "format ")) {
        cmd_format(startswith(cmd, "format "));
        return 1;
    }

    // Help
    if (strcmpi(cmd, "help") == 0 || startswith(cmd, "help ")) {
        const char *arg = cmd[4] ? cmd + 5 : "1";
        while (*arg == ' ') arg++;
        if (*arg == '2') {
            terminal_writestring("Comandos (2/3):\n");
            terminal_writestring("  dir [-a][-s] [pasta] - lista (a=ocultos, s=arvore)\n");
            terminal_writestring("  md <nome>        - cria diretorio\n");
            terminal_writestring("  cd <nome>/..     - navega dirs\n");
            terminal_writestring("  write <arq> [txt]\n");
            terminal_writestring("  cat <arq>        - mostra conteudo\n");
            terminal_writestring("  append <arq> <txt>\n");
            terminal_writestring("  copy/move <arq> [dest]\n");
            terminal_writestring("  del <arq/dir>    - remove\n");
            terminal_writestring("  rename <old> <new>\n");
            terminal_writestring("  format [a:/h:]   - formata drive\n");
            terminal_writestring("  a: / h:          - troca de drive\n");
            terminal_writestring("  edit <arq>       - editor de texto\n");
            terminal_writestring("  hexdump <arq>    - editor hexadecimal\n");
            terminal_writestring("  calc <expr>      - calculadora\n");
        } else if (*arg == '3') {
            terminal_writestring("Comandos (3/3):\n");
            terminal_writestring("  view <imagem.bmp> - visualizador BMP (320x200 216 cores)\n");
            terminal_writestring("  wget <url>        - baixa arquivo HTTP e salva no disco\n");
            terminal_writestring("  ping <host|ip>    - ICMP echo (4 pacotes)\n");
            terminal_writestring("  resolve <host>    - consulta DNS (mostra IP)\n");
            terminal_writestring("  arping <ip>       - resolucao ARP (IP -> MAC)\n");
            terminal_writestring("  arp               - mostra tabela ARP\n");
            terminal_writestring("  ifconfig          - exibe configuração de rede\n");
            terminal_writestring("  tcptest           - teste TCP (conecta e faz GET)\n");
            terminal_writestring("  beep              - toca nota no PC Speaker\n");
        } else {
            terminal_writestring("Comandos (1/3):\n");
            terminal_writestring("  help [2|3]       - ajuda (paginas 1,2,3)\n");
            terminal_writestring("  clear            - limpa tela\n");
            terminal_writestring("  reboot/poweroff\n");
            terminal_writestring("  info             - sobre o sistema\n");
            terminal_writestring("  date / uptime\n");
            terminal_writestring("  sleep <ms>\n");
            terminal_writestring("  time             - hora no prompt\n");
            terminal_writestring("  drive            - unidade no prompt\n");
            terminal_writestring("  log <txt>\n");
            terminal_writestring("  log:<X> <txt>    - cor X (0-F)\n");
            terminal_writestring("  color/bgcolor <0-F>\n");
            terminal_writestring("  meminfo\n");
            terminal_writestring("  arquivo          - executa .bin ou .cha\n");
            terminal_writestring("  balloon          - inicia a interface grafica\n");
        }
        return 1;
    }

    // Programas externos (edit, hexdump, calc)
    if (prog_run(cmd, active_drive)) return 1;

    // Binário .bin
    if (execute_binary(cmd)) return 1;

    // ---------------------------------------------------------------------------------
    // Recursos de rede
    // Comando ifconfig
    if (strcmpi(cmd, "ifconfig") == 0) {
        if (!netdev_present()) {
            terminal_writestring("Sem placa de rede.\n");
        } else {
            uint8_t m[6];
            netdev_get_mac(m);
            terminal_writestring("eth0  Driver: ");
            terminal_writestring(netdev_driver_name());
            terminal_putchar('\n');
            terminal_writestring("      MAC:    ");
            for (int i = 0; i < 6; i++) {
                print_hex_byte(m[i]);
                if (i < 5) terminal_putchar(':');
            }
            terminal_writestring("\n      Link:   UP\n");
        }
        return 1;
    }
    // Comando arp
    if (strcmpi(cmd, "arp") == 0) {
        arp_print_table();
        return 1;
    }
    // Comando arping
    if (startswith(cmd, "arping ")) {
        const char *arg = startswith(cmd, "arping ");
        // Parse IP "a.b.c.d"
        uint8_t ip[4] = {0};
        int i = 0, n = 0;
        while (*arg && n < 4) {
            if (*arg == '.') { ip[n++] = (uint8_t)i; i = 0; }
            else if (*arg >= '0' && *arg <= '9') i = i*10 + (*arg - '0');
            arg++;
        }
        ip[n] = (uint8_t)i;
        terminal_writestring("Resolvendo... ");
        uint8_t mac[6];
        if (arp_resolve(ip, mac, 2000)) {
            const char *h = "0123456789ABCDEF";
            for (int j = 0; j < 6; j++) {
                terminal_putchar(h[mac[j] >> 4]);
                terminal_putchar(h[mac[j] & 0xF]);
                if (j < 5) terminal_putchar(':');
            }
            terminal_putchar('\n');
        } else {
            terminal_writestring("Timeout.\n");
        }
        return 1;
    }
    // Comando ping
    if (startswith(cmd, "ping ")) {
        const char *arg = startswith(cmd, "ping ");
        uint8_t ip[4] = {0};

        // IP direto (começa com dígito) ou hostname
        if (arg[0] >= '0' && arg[0] <= '9') {
            int v = 0, n = 0;
            const char *p = arg;
            while (*p) {
                if (*p == '.' && n < 3) { ip[n++] = (uint8_t)v; v = 0; }
                else if (*p >= '0' && *p <= '9') v = v*10 + (*p-'0');
                p++;
            }
            ip[n] = (uint8_t)v;
        } else {
            terminal_writestring("Resolvendo ");
            terminal_writestring(arg);
            terminal_writestring("...\n");
            if (!dns_resolve(arg, ip, 3000)) {
                terminal_writestring("Falha DNS.\n");
                return 1;
            }
        }

        // Mostra IP e envia 4 pings (código já existente)
        terminal_writestring("PING ");
        for (int j = 0; j < 4; j++) {
            uint8_t b = ip[j];
            if (b >= 100) terminal_putchar('0' + b/100);
            if (b >= 10)  terminal_putchar('0' + (b/10)%10);
            terminal_putchar('0' + b%10);
            if (j < 3) terminal_putchar('.');
        }
        terminal_putchar('\n');
        for (uint16_t seq = 0; seq < 4; seq++) {
            int rtt = icmp_ping(ip, seq, 2000);
            terminal_writestring("seq=");
            terminal_putchar('0' + seq);
            if (rtt >= 0) {
                terminal_writestring(" time=");
                if (rtt >= 100) terminal_putchar('0' + (rtt/100)%10);
                if (rtt >= 10)  terminal_putchar('0' + (rtt/10)%10);
                terminal_putchar('0' + rtt%10);
                terminal_writestring("ms\n");
            } else {
                terminal_writestring(" timeout\n");
            }
        }
        return 1;
    }
    // Comando resolve
    if (startswith(cmd, "resolve ")) {
        const char *host = startswith(cmd, "resolve ");
        uint8_t ip[4];
        terminal_writestring("Resolvendo... ");
        if (dns_resolve(host, ip, 3000)) {
            for (int j = 0; j < 4; j++) {
                uint8_t b = ip[j];
                if (b >= 100) terminal_putchar('0' + b/100);
                if (b >= 10)  terminal_putchar('0' + (b/10)%10);
                terminal_putchar('0' + b%10);
                if (j < 3) terminal_putchar('.');
            }
            terminal_putchar('\n');
        } else {
            terminal_writestring("Falha.\n");
        }
        return 1;
    }
    if (strcmpi(cmd, "tcptest") == 0) {
        static TcpConn conn;
        uint8_t ip[4];

        terminal_writestring("Resolvendo freeroot.ne-t.org...\n");
        if (!dns_resolve("freeroot.ne-t.org", ip, 3000)) {
            terminal_writestring("DNS falhou.\n"); return 1;
        }

        terminal_writestring("Conectando na porta 80...\n");
        if (!tcp_connect(&conn, ip, 80, 5000)) {
            terminal_writestring("Falha TCP.\n"); return 1;
        }
        terminal_writestring("Conectado! Enviando GET...\n");

        const char *req = "GET / HTTP/1.0\r\nHost: freeroot.ne-t.org\r\n\r\n";
        uint16_t rlen = 0; while (req[rlen]) rlen++;
       tcp_send(&conn, (const uint8_t *)req, rlen);

        uint8_t buf[512];
        int n;
        while ((n = tcp_recv(&conn, buf, 511, 3000)) > 0) {
            buf[n] = 0;
            terminal_writestring((char *)buf);
        }
        tcp_close(&conn);
        terminal_putchar('\n');
        return 1;
    }
    // Comando wget
    if (startswith(cmd, "wget ")) {
        const char *url = startswith(cmd, "wget ");

        // Suporta "http://" ou sem prefixo
        if (url[0]=='h' && url[1]=='t' && url[2]=='t' && url[3]=='p' &&
            url[4]==':' && url[5]=='/' && url[6]=='/') url += 7;

        // Extrai hostname e path
        char host[64] = {0};
        const char *path = "/";
        int hi = 0;
        while (url[hi] && url[hi] != '/' && hi < 63) {
            host[hi] = url[hi]; hi++;
        }
        host[hi] = 0;
        if (url[hi] == '/') path = url + hi;

        // Extrai nome do arquivo do path
        const char *fname = path;
        for (int i = 0; path[i]; i++)
            if (path[i] == '/') fname = path + i + 1;
        if (!fname[0]) fname = "index.html";

        char fname_buf[32];
        int fi = 0;
        while (fname[fi] && fname[fi] != '?' && fi < 31)
            { fname_buf[fi] = fname[fi]; fi++; }
        fname_buf[fi] = 0;
        if (!fname_buf[0]) { fname_buf[0]='i'; fname_buf[1]='n'; fname_buf[2]='d';
                             fname_buf[3]='e'; fname_buf[4]='x'; fname_buf[5]='.';
                             fname_buf[6]='h'; fname_buf[7]='t'; fname_buf[8]='m';
                             fname_buf[9]='l'; fname_buf[10]=0; }

        // Resolve DNS
        terminal_writestring("Resolvendo ");
        terminal_writestring(host);
        terminal_writestring("...\n");
        uint8_t ip[4];
        if (!dns_resolve(host, ip, 3000)) {
            terminal_writestring("DNS falhou.\n"); return 1;
        }

        // Conecta TCP
        terminal_writestring("Conectando...\n");
        static TcpConn conn;
        if (!tcp_connect(&conn, ip, 80, 5000)) {
            terminal_writestring("Falha TCP.\n"); return 1;
        }

        // Envia request HTTP/1.0
        static char req[256];
        int rpos = 0;
        const char *g1 = "GET "; const char *g2 = " HTTP/1.0\r\nHost: ";
        const char *g3 = "\r\n\r\n";
        for (int i = 0; g1[i]; i++) req[rpos++] = g1[i];
        for (int i = 0; path[i]; i++) req[rpos++] = path[i];
        for (int i = 0; g2[i]; i++) req[rpos++] = g2[i];
        for (int i = 0; host[i]; i++) req[rpos++] = host[i];
        for (int i = 0; g3[i]; i++) req[rpos++] = g3[i];
        tcp_send(&conn, (const uint8_t *)req, (uint16_t)rpos);

        // Recebe resposta completa
        uint32_t total = 0;
        uint8_t  chunk[512];
        int n;
        while ((n = tcp_recv(&conn, chunk, 512, 3000)) > 0) {
            if (total + n > sizeof(wget_body)) n = (int)(sizeof(wget_body) - total);
            for (int i = 0; i < n; i++) wget_body[total + i] = chunk[i];
            total += n;
            if (total >= sizeof(wget_body)) break;
        }
        tcp_close(&conn);

        if (total == 0) { terminal_writestring("Sem resposta.\n"); return 1; }

        // Pula headers HTTP (procura \r\n\r\n)
        uint32_t body_start = 0;
        for (uint32_t i = 0; i + 3 < total; i++) {
            if (wget_body[i]=='\r' && wget_body[i+1]=='\n' &&
                wget_body[i+2]=='\r' && wget_body[i+3]=='\n') {
                body_start = i + 4; break;
            }
        }

        uint32_t body_len = total - body_start;

        // Mostra status HTTP
        wget_body[total] = 0;
        terminal_writestring("HTTP: ");
        for (int i = 9; wget_body[i] && wget_body[i] != '\r'; i++)
            terminal_putchar(wget_body[i]);
        terminal_putchar('\n');

        if (body_len == 0) { terminal_writestring("Body vazio.\n"); return 1; }

        // Salva no filesystem
        char fname_name[9] = {0}, fname_ext[4] = {0};
        split_filename(fname_buf, fname_name, 9, fname_ext, 4);

        int r = drv_write(fname_name, fname_ext,
                          wget_body + body_start, body_len);
        if (r >= 0) {
            terminal_writestring("Salvo: ");
            terminal_writestring(fname_name);
            if (fname_ext[0]) { terminal_putchar('.'); terminal_writestring(fname_ext); }
            terminal_writestring(" (");
            print_uint(body_len);
            terminal_writestring(" bytes)\n");
        } else {
            terminal_writestring("Erro ao salvar.\n");
        }
        return 1;
    }

    // Outros
    // Script .cha
    char *script = load_script(cmd);
    if (script) {
        run_script(script);
        kfree(script);
        return 1;
    }

    terminal_writestring("Comando desconhecido: ");
    terminal_writestring(cmd);
    terminal_putchar('\n');
    return 0;
}

// ----------------------------------------------------------------
// Parsing de múltiplos comandos separados por ';' (com expansão de variáveis)
// ----------------------------------------------------------------
void parse_and_execute(char *line) {
    // Primeiro expande variáveis na linha toda
    char expanded[512];
    expand_variables(line, expanded, sizeof(expanded));

    char copy[512];
    int ci = 0;
    while (expanded[ci] && ci < 511) { copy[ci] = expanded[ci]; ci++; }
    copy[ci] = '\0';
    char *start = copy;
    char *p = copy;
    while (1) {
        if (*p == ';' || *p == '\0') {
            char saved = *p;
            if (saved == ';') *p = '\0';
            char *cmd = start;
            while (*cmd == ' ') cmd++;
            char *end = cmd;
            while (*end) end++;
            end--;
            while (end > cmd && (*end == ' ' || *end == '\t')) *end-- = '\0';
            if (*cmd != '\0') {
                execute_command(cmd);
            }
            if (saved == '\0') break;
            *p = ';';
            start = p + 1;
            p = start;
        } else {
            p++;
        }
    }
}

// ----------------------------------------------------------------
// Salva/restaura estado do shell nos terminais
// ----------------------------------------------------------------
static void tty_save_shell_state(void) {
    tty_t *t = tty_active();
    t->drive     = active_drive;
    t->show_time = show_time;
    t->show_drive= show_drive;
    t->cwd_disk  = fsd_cwd();
    t->cwd_heap  = fs_cwd();
    t->hist_count= hist_count;
    t->hist_head = hist_head;
    for (int i = 0; i < HIST_MAX; i++)
        for (int j = 0; j < HIST_LEN; j++)
            t->hist[i][j] = hist[i][j];
}

static void tty_restore_shell_state(void) {
    tty_t *t = tty_active();
    active_drive = t->drive;
    show_time    = t->show_time;
    show_drive   = t->show_drive;
    fsd_set_cwd(t->cwd_disk);
    fs_set_cwd(t->cwd_heap);
    hist_count   = t->hist_count;
    hist_head    = t->hist_head;
    for (int i = 0; i < HIST_MAX; i++)
        for (int j = 0; j < HIST_LEN; j++)
            hist[i][j] = t->hist[i][j];
}

static int do_tty_switch(int n) {
    if (n == tty_current()) return 0;
    tty_save_shell_state();
    tty_save();
    if (tty_switch(n) != 0) {
        tty_restore(tty_current());
        terminal_writestring("Terminal nao disponivel.\n");
        return -1;
    }
    tty_restore_shell_state();
    return 0;
}

const char *vfs_read(const char *name, const char *ext) {
    return drv_read(name, ext);
}
int vfs_write(const char *name, const char *ext,
              const char *data, uint32_t size) {
    return drv_write(name, ext, data, size);
}
int vfs_delete(const char *name, const char *ext, int is_dir) {
    return drv_delete(name, ext, is_dir);
}

// ----------------------------------------------------------------
// Shell principal
// ----------------------------------------------------------------
void shell_run(void) {
    char buffer[256];

    // Executa run_cmd do terminal na primeira entrada
    if (tty_run_init(tty_current())) {
        const char *rc = tty_active()->run_cmd;
        if (rc && rc[0]) parse_and_execute((char*)rc);
    }

    while (1) {
        if (tty_switch_pending >= 0) {
            int n = tty_switch_pending;
            tty_switch_pending = -1;
            if (do_tty_switch(n) == 0) {
                if (tty_run_init(tty_current())) {
                    const char *rp = tty_active()->run_cmd;
                    if (rp && rp[0]) parse_and_execute((char*)rp);
                }
            }
            continue;
        }

        terminal_putchar('\n');
        if (show_time) {
            uint8_t h = bcd_to_bin(rtc_read(0x04));
            uint8_t m = bcd_to_bin(rtc_read(0x02));
            uint8_t s = bcd_to_bin(rtc_read(0x00));
            terminal_putchar('[');
            print_dec2(h); terminal_putchar(':');
            print_dec2(m); terminal_putchar(':');
            print_dec2(s); terminal_writestring("] ");
        }
	if (show_drive) {
	    terminal_putchar(active_drive);
	    terminal_putchar(':');
	}
	char pathbuf[64];
	drv_cwd_path(pathbuf, sizeof(pathbuf));
	terminal_writestring(pathbuf[0] ? pathbuf : "/");
	terminal_writestring("> ");

        readline(buffer, sizeof(buffer));
        hist_push(buffer);
        parse_and_execute(buffer);
    }
}
