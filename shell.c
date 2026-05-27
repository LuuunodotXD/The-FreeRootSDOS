// shell.c
#include "terminal.h"
#include "keyboard.h"
#include "shell.h"
#include "io.h"
#include "idt.h"
#include "kmalloc.h"
#include "fs.h"

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

// Retorna ponteiro para após o prefixo (case-insensitive), ou 0
static const char *startswith(const char *str, const char *prefix) {
    while (*prefix)
        if (to_lower(*str++) != to_lower(*prefix++)) return 0;
    return str;
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
    if (n == 0) { terminal_putchar('0'); return; }
    while (n > 0) { buf[i++] = '0' + n % 10; n /= 10; }
    while (i-- > 0) terminal_putchar(buf[i]);
}

static void print_kb(uint32_t bytes) {
    print_uint(bytes / 1024);
    terminal_writestring(" KB");
}

// ----------------------------------------------------------------
// Histórico de comandos
// ----------------------------------------------------------------

#define HIST_MAX 10
#define HIST_LEN 256

static char hist[HIST_MAX][HIST_LEN];
static int  hist_count = 0;
static int  hist_head  = 0;

static void hist_push(const char *cmd) {
    if (!cmd[0]) return;
    if (hist_count > 0) {
        int last = (hist_head - 1 + HIST_MAX) % HIST_MAX;
        const char *a = hist[last]; const char *b = cmd;
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

static int show_time = 0;   // 1 = mostra hora no prompt

static const char *hist_get(int idx) {
    if (idx < 0 || idx >= hist_count) return 0;
    return hist[(hist_head - 1 - idx + HIST_MAX * 2) % HIST_MAX];
}

// ----------------------------------------------------------------
// RTC
// ----------------------------------------------------------------

static uint8_t bcd_to_bin(uint8_t v) { return (v >> 4) * 10 + (v & 0xF); }
static uint8_t rtc_read(uint8_t reg)  { outb(0x70, reg); return inb(0x71); }

// ----------------------------------------------------------------
// Reboot / Poweroff
// ----------------------------------------------------------------

void shell_init(void) {}

static void reboot(void) {
    while (inb(0x64) & 0x02);
    outb(0x64, 0xFE);
    while (1) asm volatile ("hlt");
}

static void poweroff(void) {
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    terminal_writestring("Poweroff nao suportado neste hardware.\n");
    while (1) asm volatile ("hlt");
}

// ----------------------------------------------------------------
// Leitura de linha com edição completa
// ----------------------------------------------------------------

static void redraw_from(const char *buf, int from, int len, int cur) {
    for (int i = from; i < len; i++) terminal_putchar(buf[i]);
    terminal_putchar(' ');
    terminal_cursor_left(len - from + 1);
    terminal_cursor_right(cur - from);
}

static void readline(char *buf, int maxlen) {
    int len = 0, cur = 0, hist_idx = -1;
    char saved[HIST_LEN]; saved[0] = '\0';

    while (1) {
        int k = getchar();

        if (k == '\n') { buf[len] = '\0'; terminal_putchar('\n'); return; }

        if (k == '\b') {
            if (cur == 0) continue;
            for (int i = cur - 1; i < len - 1; i++) buf[i] = buf[i+1];
            len--; cur--;
            terminal_cursor_left(1);
            redraw_from(buf, cur, len, cur);
            continue;
        }
        if (k == KEY_DEL) {
            if (cur == len) continue;
            for (int i = cur; i < len - 1; i++) buf[i] = buf[i+1];
            len--;
            redraw_from(buf, cur, len, cur);
            continue;
        }
        if (k == KEY_LEFT)  { if (cur > 0)   { cur--; terminal_cursor_left(1); }  continue; }
        if (k == KEY_RIGHT) { if (cur < len) { cur++; terminal_cursor_right(1); } continue; }
        if (k == KEY_HOME)  { terminal_cursor_left(cur); cur = 0; continue; }
        if (k == KEY_END)   { terminal_cursor_right(len - cur); cur = len; continue; }

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
            for (int i = len; i > cur; i--) buf[i] = buf[i-1];
            buf[cur] = (char)k;
            len++; cur++;
            for (int i = cur - 1; i < len; i++) terminal_putchar(buf[i]);
            terminal_cursor_left(len - cur);
        }
    }
}

// ----------------------------------------------------------------
// Comandos do sistema de arquivos
// ----------------------------------------------------------------


static void cmd_append(const char *arg) {
    // Sintaxe: append nome.ext texto
    char name[FS_NAME_LEN + 1], ext[FS_EXT_LEN + 1];
    int i = 0;
    while (arg[i] && arg[i] != ' ') i++;
    if (!arg[i]) { terminal_writestring("Uso: append <arquivo> <texto>\n"); return; }

    char fname[16];
    for (int j = 0; j < i && j < 15; j++) fname[j] = arg[j];
    fname[i] = '\0';
    fs_split(fname, name, ext);

    const char *add = arg + i + 1;
    if (!add[0]) { terminal_writestring("Uso: append <arquivo> <texto>\n"); return; }

    // Le conteudo atual
    const char *cur = fs_read(name, ext);
    uint32_t cur_len = 0;
    if (cur) while (cur[cur_len]) cur_len++;

    uint32_t add_len = 0;
    while (add[add_len]) add_len++;

    // Monta novo conteudo: atual + newline + novo texto
    uint32_t new_len = cur_len + (cur_len ? 1 : 0) + add_len;
    char *buf = (char *)kmalloc(new_len + 1);
    if (!buf) { terminal_writestring("Sem memoria.\n"); return; }

    uint32_t pos = 0;
    for (uint32_t j = 0; j < cur_len; j++) buf[pos++] = cur[j];
    if (cur_len) buf[pos++] = '\n';
    for (uint32_t j = 0; j < add_len; j++) buf[pos++] = add[j];
    buf[pos] = '\0';

    int ret = fs_write(name, ext, buf, new_len);
    kfree(buf);
    if (ret == 0) terminal_writestring("Linha adicionada.\n");
    else terminal_writestring("Erro ao escrever arquivo.\n");
}

static void cmd_dir(void) {
    fs_file_t *t = fs_table();
    int count = 0;
    for (int i = 0; i < fs_max(); i++) {
        if (!t[i].used) continue;
        terminal_writestring(t[i].name);
        if (t[i].ext[0]) {
            terminal_putchar('.');
            terminal_writestring(t[i].ext);
        }
        terminal_writestring("  ");
        print_uint(t[i].size);
        terminal_writestring(" bytes\n");
        count++;
    }
    if (count == 0) terminal_writestring("Nenhum arquivo.\n");
}

static void cmd_write(const char *arg) {
    // Sintaxe: write nome.ext conteudo
    // ou:      write nome.ext          (arquivo vazio)
    char name[FS_NAME_LEN + 1], ext[FS_EXT_LEN + 1];

    // Extrai primeiro token (nome do arquivo)
    int i = 0;
    while (arg[i] && arg[i] != ' ' && i < 15) i++;
    char fname[16];
    for (int j = 0; j < i; j++) fname[j] = arg[j];
    fname[i] = '\0';

    if (!fname[0]) { terminal_writestring("Uso: write <arquivo> [conteudo]\n"); return; }

    fs_split(fname, name, ext);

    const char *content = arg[i] ? arg + i + 1 : 0;
    uint32_t size = 0;
    if (content) while (content[size]) size++;

    int ret = fs_write(name, ext, content, size);
    if (ret ==  0) terminal_writestring("Arquivo salvo.\n");
    else if (ret == -2) terminal_writestring("Nome invalido. Use letras, numeros, _ ou -\n");
    else terminal_writestring("Erro: sem espaco no disco.\n");
}

static void cmd_cat(const char *arg) {
    char name[FS_NAME_LEN + 1], ext[FS_EXT_LEN + 1];
    fs_split(arg, name, ext);
    const char *data = fs_read(name, ext);
    if (!data) terminal_writestring("Arquivo nao encontrado ou vazio.\n");
    else { terminal_writestring(data); terminal_putchar('\n'); }
}

static void cmd_del(const char *arg) {
    char name[FS_NAME_LEN + 1], ext[FS_EXT_LEN + 1];
    fs_split(arg, name, ext);
    if (fs_delete(name, ext) == 0) terminal_writestring("Deletado.\n");
    else terminal_writestring("Arquivo nao encontrado.\n");
}

static void cmd_rename(const char *arg) {
    // Sintaxe: rename antigo.ext novo.ext
    char old_n[FS_NAME_LEN+1], old_e[FS_EXT_LEN+1];
    char new_n[FS_NAME_LEN+1], new_e[FS_EXT_LEN+1];

    int i = 0;
    while (arg[i] && arg[i] != ' ') i++;
    if (!arg[i]) { terminal_writestring("Uso: rename <antigo> <novo>\n"); return; }

    char a[16], b[16];
    int j = 0;
    while (j < i && j < 15) { a[j] = arg[j]; j++; } a[j] = '\0';
    const char *rest = arg + i + 1;
    j = 0;
    while (rest[j] && j < 15) { b[j] = rest[j]; j++; } b[j] = '\0';

    fs_split(a, old_n, old_e);
    fs_split(b, new_n, new_e);

    int ret = fs_rename(old_n, old_e, new_n, new_e);
    if (ret ==  0) terminal_writestring("Renomeado.\n");
    else if (ret == -1) terminal_writestring("Arquivo nao encontrado.\n");
    else terminal_writestring("Nome de destino ja existe.\n");
}

// ----------------------------------------------------------------
// Shell principal
// ----------------------------------------------------------------

void shell_run(void) {
    char buffer[256];

    while (1) {
        if (show_time) {
            uint8_t h  = bcd_to_bin(rtc_read(0x04));
            uint8_t m  = bcd_to_bin(rtc_read(0x02));
            uint8_t sc = bcd_to_bin(rtc_read(0x00));
            terminal_writestring("\n[");
            print_dec2(h); terminal_putchar(':');
            print_dec2(m); terminal_putchar(':');
            print_dec2(sc);
            terminal_writestring("] > ");
        } else {
            terminal_writestring("\n> ");
        }
        readline(buffer, sizeof(buffer));
        hist_push(buffer);

        // ---- Sistema ----
        if (strcmpi(buffer, "help") == 0 || startswith(buffer, "help ")) {
            const char *arg = buffer[4] ? buffer + 5 : "1";
            while (*arg == ' ') arg++;
            int page = (*arg == '2') ? 2 : 1;
            if (page == 1) {
                terminal_writestring("Comandos (1/2):\n");
                terminal_writestring("  help [2]         - ajuda (pag 1 ou 2)\n");
                terminal_writestring("  clear            - limpa a tela\n");
                terminal_writestring("  reboot           - reinicia\n");
                terminal_writestring("  poweroff         - desliga\n");
                terminal_writestring("  info             - sobre o sistema\n");
                terminal_writestring("  date             - data e hora\n");
                terminal_writestring("  uptime           - tempo desde o boot\n");
                terminal_writestring("  sleep <ms>       - pausa em ms\n");
                terminal_writestring("  log <texto>      - imprime mensagem\n");
                terminal_writestring("  log:<X> <texto>  - imprime na cor X (0-F)\n");
                terminal_writestring("  color <0-F>      - cor do texto\n");
                terminal_writestring("  bgcolor <0-F>    - cor de fundo\n");
                terminal_writestring("  meminfo          - uso do heap\n");
                terminal_writestring("  time             - ativa/desativa hora no prompt\n");
            } else {
                terminal_writestring("Comandos (2/2):\n");
                terminal_writestring("  dir              - lista arquivos\n");
                terminal_writestring("  write <arq> [texto] - cria/edita arquivo\n");
                terminal_writestring("  cat <arq>        - mostra conteudo\n");
                terminal_writestring("  del <arq>        - remove arquivo\n");
                terminal_writestring("  append <arq> <texto> - adiciona linha\n");
                terminal_writestring("  rename <old> <new>  - renomeia\n");
                terminal_writestring("  format           - apaga todos os arquivos\n");
            }
        }
        else if (strcmpi(buffer, "clear") == 0) { terminal_clear(); }
        else if (strcmpi(buffer, "reboot") == 0) {
            terminal_writestring("Reiniciando...\n"); reboot();
        }
        else if (strcmpi(buffer, "poweroff") == 0) {
            terminal_writestring("Desligando...\n"); poweroff();
        }
        else if (strcmpi(buffer, "info") == 0) {
            terminal_writestring("FreeRootSDOS v0.3\n");
            terminal_writestring("Modo protegido 32-bit, IDT/IRQ, VGA, shell interativa.\n");
        }
        else if (strcmpi(buffer, "date") == 0) {
            uint8_t sec = bcd_to_bin(rtc_read(0x00));
            uint8_t min = bcd_to_bin(rtc_read(0x02));
            uint8_t hr  = bcd_to_bin(rtc_read(0x04));
            uint8_t day = bcd_to_bin(rtc_read(0x07));
            uint8_t mon = bcd_to_bin(rtc_read(0x08));
            uint8_t yr  = bcd_to_bin(rtc_read(0x09));
            print_dec2(day); terminal_putchar('/');
            print_dec2(mon); terminal_putchar('/');
            terminal_writestring("20");
            print_dec2(yr);  terminal_writestring("  ");
            print_dec2(hr);  terminal_putchar(':');
            print_dec2(min); terminal_putchar(':');
            print_dec2(sec); terminal_putchar('\n');
        }
        else if (strcmpi(buffer, "uptime") == 0) {
            uint32_t ms = timer_get_ticks();
            uint32_t s  = ms / 1000;
            terminal_writestring("Uptime: ");
            print_dec2(s / 3600);          terminal_putchar(':');
            print_dec2((s % 3600) / 60);   terminal_putchar(':');
            print_dec2(s % 60);            terminal_putchar('\n');
        }
        else if (startswith(buffer, "sleep ")) {
            const char *arg = startswith(buffer, "sleep ");
            uint32_t ms = 0;
            while (*arg >= '0' && *arg <= '9') ms = ms * 10 + (*arg++ - '0');
            if (ms == 0 || *arg != '\0') terminal_writestring("Uso: sleep <ms>\n");
            else { timer_sleep(ms); terminal_writestring("Pronto.\n"); }
        }

        // ---- Log com cor: log:<X> texto  ou  log texto ----
        else if (startswith(buffer, "log:")) {
            const char *arg = startswith(buffer, "log:");
            int v = hexdigit(arg[0]);
            if (v < 0 || arg[1] != ' ') {
                terminal_writestring("Uso: log:<0-F> <texto>  ex: log:C erro critico\n");
            } else {
                uint8_t old = terminal_get_fg();
                terminal_set_fg((uint8_t)v);
                terminal_writestring(arg + 2);
                terminal_putchar('\n');
                terminal_set_fg(old);
            }
        }
        else if (startswith(buffer, "log ")) {
            terminal_writestring(startswith(buffer, "log "));
            terminal_putchar('\n');
        }

        // ---- Cores ----
        else if (startswith(buffer, "color ")) {
            const char *arg = startswith(buffer, "color ");
            int v = hexdigit(arg[0]);
            if (v < 0 || arg[1] != '\0') terminal_writestring("Uso: color <0-F>\n");
            else { terminal_set_fg((uint8_t)v); terminal_writestring("Cor alterada.\n"); }
        }
        else if (startswith(buffer, "bgcolor ")) {
            const char *arg = startswith(buffer, "bgcolor ");
            int v = hexdigit(arg[0]);
            if (v < 0 || arg[1] != '\0') terminal_writestring("Uso: bgcolor <0-F>\n");
            else { terminal_set_bg((uint8_t)v); terminal_clear(); terminal_writestring("Cor de fundo alterada.\n"); }
        }

        // ---- Meminfo ----
        else if (strcmpi(buffer, "meminfo") == 0) {
            uint32_t total = 64u * 1024u;
            uint32_t free  = kmalloc_free();
            terminal_writestring("Heap total: "); print_kb(total); terminal_putchar('\n');
            terminal_writestring("Heap usado: "); print_kb(total - free); terminal_putchar('\n');
            terminal_writestring("Heap livre: "); print_kb(free);  terminal_putchar('\n');
        }

        // ---- Sistema de arquivos ----
        else if (strcmpi(buffer, "dir") == 0) { cmd_dir(); }
        else if (startswith(buffer, "write "))  { cmd_write(startswith(buffer, "write ")); }
        else if (startswith(buffer, "cat "))    { cmd_cat(startswith(buffer, "cat ")); }
        else if (startswith(buffer, "del "))    { cmd_del(startswith(buffer, "del ")); }
        else if (startswith(buffer, "append ")) { cmd_append(startswith(buffer, "append ")); }
        else if (startswith(buffer, "rename ")) { cmd_rename(startswith(buffer, "rename ")); }
        else if (strcmpi(buffer, "format") == 0) {
            fs_format();
            terminal_writestring("Drive formatado.\n");
        }

        else if (strcmpi(buffer, "time") == 0) {
            show_time ^= 1;
            terminal_writestring(show_time ? "Hora no prompt ativada.\n"
                                           : "Hora no prompt desativada.\n");
        }
        else if (buffer[0] != '\0') {
            terminal_writestring("Comando desconhecido: ");
            terminal_writestring(buffer);
            terminal_putchar('\n');
        }
    }
}
