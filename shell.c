// shell.c
#include "terminal.h"
#include "keyboard.h"
#include "shell.h"
#include "io.h"
#include "idt.h"
#include "kmalloc.h"

// ----------------------------------------------------------------
// Utilitários
// ----------------------------------------------------------------

static char to_lower(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    return c;
}

// strcmp case-insensitive
static int strcmpi(const char *a, const char *b) {
    while (*a && *b && to_lower(*a) == to_lower(*b)) { a++; b++; }
    return to_lower(*a) - to_lower(*b);
}

// startswith case-insensitive, retorna ponteiro para o restante ou 0
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


// ----------------------------------------------------------------
// Historico de comandos (ultimos 10, some ao reiniciar)
// ----------------------------------------------------------------

#define HIST_MAX  10
#define HIST_LEN  256

static char hist[HIST_MAX][HIST_LEN];  // buffer circular
static int  hist_count = 0;            // total de entradas gravadas (max HIST_MAX)
static int  hist_head  = 0;            // proximo slot de escrita

// Salva um comando no historico (ignora strings vazias e duplicata consecutiva)
static void hist_push(const char *cmd) {
    if (!cmd[0]) return;
    // Nao grava se igual ao ultimo
    if (hist_count > 0) {
        int last = (hist_head - 1 + HIST_MAX) % HIST_MAX;
        int eq = 1;
        const char *a = hist[last], *b = cmd;
        while (*a || *b) { if (*a++ != *b++) { eq = 0; break; } }
        if (eq) return;
    }
    // Copia para o slot atual
    int i = 0;
    while (cmd[i] && i < HIST_LEN - 1) { hist[hist_head][i] = cmd[i]; i++; }
    hist[hist_head][i] = '\0';
    hist_head = (hist_head + 1) % HIST_MAX;
    if (hist_count < HIST_MAX) hist_count++;
}

// Retorna entrada do historico: idx=0 e o mais recente, idx=1 o anterior, etc.
// Retorna 0 se idx >= hist_count
static const char *hist_get(int idx) {
    if (idx < 0 || idx >= hist_count) return 0;
    int slot = (hist_head - 1 - idx + HIST_MAX * 2) % HIST_MAX;
    return hist[slot];
}

// ----------------------------------------------------------------
// Uptime via tick counter (PIT a 1000 Hz = 1 tick por ms)
// ----------------------------------------------------------------

// ----------------------------------------------------------------
// RTC (CMOS) -- leitura de data e hora
// ----------------------------------------------------------------

static uint8_t bcd_to_bin(uint8_t v) { return (v >> 4) * 10 + (v & 0xF); }

static uint8_t rtc_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static void print_kb(uint32_t bytes) {
    uint32_t kb = bytes / 1024;
    char buf[8]; int i = 0;
    if (kb == 0) { terminal_putchar('0'); }
    else { while (kb > 0) { buf[i++] = '0' + kb % 10; kb /= 10; }
           while (i-- > 0) terminal_putchar(buf[i]); }
    terminal_writestring(" KB");
}

void shell_init(void) { /* nada a inicializar — ticks comecam em 0 no boot */ }

// ----------------------------------------------------------------
// Poweroff / Reboot
// ----------------------------------------------------------------

static void poweroff(void) {
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    terminal_writestring("Poweroff nao suportado neste hardware.\n");
    while (1) asm volatile ("hlt");
}

static void reboot(void) {
    while (inb(0x64) & 0x02);
    outb(0x64, 0xFE);
    while (1) asm volatile ("hlt");
}

// ----------------------------------------------------------------
// log com cor opcional:  log /X\ texto
// ----------------------------------------------------------------

static void cmd_log(const char *arg) {
    if (arg[0] == '/' && hexdigit(arg[1]) >= 0 && arg[2] == '\\') {
        uint8_t old = terminal_get_fg();
        terminal_set_fg((uint8_t)hexdigit(arg[1]));
        const char *text = arg + 3;
        if (*text == ' ') text++;
        terminal_writestring(text);
        terminal_putchar('\n');
        terminal_set_fg(old);
    } else {
        terminal_writestring(arg);
        terminal_putchar('\n');
    }
}

// ----------------------------------------------------------------
// Leitura de linha com cursor móvel
//
// buffer[0..len-1] = conteúdo digitado
// cur              = posição do cursor dentro do buffer (0..len)
//
// Home  → cur = 0
// End   → cur = len
// ←     → cur--
// →     → cur++
// Del   → apaga caractere na posição cur (não recua)
// Bksp  → apaga caractere antes de cur (recua)
//
// Para mover o cursor visualmente sem redesenhar a linha toda,
// usamos terminal_cursor_left / terminal_cursor_right.
// Quando há inserção/deleção no meio, redesenhamos do cur até len.
// ----------------------------------------------------------------

static void redraw_from(const char *buf, int from, int len, int cur) {
    // Redesenha buf[from..len-1] e apaga o caractere extra que pode ter sobrado
    for (int i = from; i < len; i++)
        terminal_putchar(buf[i]);
    terminal_putchar(' ');                    // apaga possivel sobra
    terminal_cursor_left(len - from + 1);     // volta o cursor para after 'from'
    terminal_cursor_right(cur - from);        // avanca ate a posicao cur
}

static void readline(char *buf, int maxlen) {
    int len = 0, cur = 0;
    int hist_idx = -1;   // -1 = digitando nova linha; 0..N-1 = navegando historico
    char saved[HIST_LEN]; saved[0] = '\0';  // salva linha em edicao ao navegar

    while (1) {
        int k = getchar();

        if (k == '\n') {
            buf[len] = '\0';
            terminal_putchar('\n');
            return;
        }

        // ---- Backspace ----
        if (k == '\b') {
            if (cur == 0) continue;
            // Remove buf[cur-1], move tudo uma posição à esquerda
            for (int i = cur - 1; i < len - 1; i++) buf[i] = buf[i+1];
            len--; cur--;
            terminal_cursor_left(1);
            redraw_from(buf, cur, len, cur);
            continue;
        }

        // ---- Delete ----
        if (k == KEY_DEL) {
            if (cur == len) continue;
            for (int i = cur; i < len - 1; i++) buf[i] = buf[i+1];
            len--;
            redraw_from(buf, cur, len, cur);
            continue;
        }

        // ---- Seta esquerda ----
        if (k == KEY_LEFT) {
            if (cur > 0) { cur--; terminal_cursor_left(1); }
            continue;
        }

        // ---- Seta direita ----
        if (k == KEY_RIGHT) {
            if (cur < len) { cur++; terminal_cursor_right(1); }
            continue;
        }

        // ---- Seta cima: historico anterior ----
        if (k == KEY_UP) {
            if (hist_idx + 1 >= hist_count) continue;  // nao tem mais
            if (hist_idx == -1) {
                // Salva o que estava sendo digitado
                int i = 0;
                while (i < len) { saved[i] = buf[i]; i++; }
                saved[i] = '\0';
            }
            hist_idx++;
            const char *entry = hist_get(hist_idx);
            // Apaga linha atual na tela
            terminal_cursor_left(cur);
            for (int i = 0; i < len; i++) terminal_putchar(' ');
            terminal_cursor_left(len);
            // Copia entrada do historico para buf
            len = 0;
            while (entry[len] && len < maxlen - 1) { buf[len] = entry[len]; len++; }
            buf[len] = '\0'; cur = len;
            terminal_writestring(buf);
            continue;
        }

        // ---- Seta baixo: historico mais recente / volta para edicao ----
        if (k == KEY_DOWN) {
            if (hist_idx == -1) continue;  // ja na linha nova
            hist_idx--;
            const char *entry = (hist_idx == -1) ? saved : hist_get(hist_idx);
            // Apaga linha atual na tela
            terminal_cursor_left(cur);
            for (int i = 0; i < len; i++) terminal_putchar(' ');
            terminal_cursor_left(len);
            // Copia entrada para buf
            len = 0;
            while (entry[len] && len < maxlen - 1) { buf[len] = entry[len]; len++; }
            buf[len] = '\0'; cur = len;
            terminal_writestring(buf);
            continue;
        }

        // ---- Home ----
        if (k == KEY_HOME) {
            terminal_cursor_left(cur);
            cur = 0;
            continue;
        }

        // ---- End ----
        if (k == KEY_END) {
            terminal_cursor_right(len - cur);
            cur = len;
            continue;
        }

        // ---- Caractere imprimivel ----
        if (k >= ' ' && k <= '~' && len < maxlen - 1) {
            // Abre espaço em buf[cur]
            for (int i = len; i > cur; i--) buf[i] = buf[i-1];
            buf[cur] = (char)k;
            len++; cur++;
            // Redesenha do ponto de inserção
            for (int i = cur - 1; i < len; i++) terminal_putchar(buf[i]);
            terminal_cursor_left(len - cur);   // volta cursor para após o char inserido
        }
    }
}

// ----------------------------------------------------------------
// Shell principal
// ----------------------------------------------------------------

void shell_run(void) {
    char buffer[256];

    while (1) {
        terminal_writestring("\n> ");
        readline(buffer, sizeof(buffer));
        hist_push(buffer);

        if (strcmpi(buffer, "help") == 0) {
            terminal_writestring("Comandos disponiveis:\n");
            terminal_writestring("  help              - mostra esta ajuda\n");
            terminal_writestring("  clear             - limpa a tela\n");
            terminal_writestring("  reboot            - reinicia o sistema\n");
            terminal_writestring("  poweroff          - desliga o sistema\n");
            terminal_writestring("  info              - informacoes do SO\n");
            terminal_writestring("  log <texto>       - imprime uma mensagem\n");
            terminal_writestring("  log /X\\ <texto>   - imprime na cor X (0-F)\n");
            terminal_writestring("  color <0-F>       - cor do texto\n");
            terminal_writestring("  bgcolor <0-F>     - cor de fundo\n");
            terminal_writestring("  uptime            - tempo desde o boot\n");
            terminal_writestring("  sleep <ms>        - pausa por N milissegundos\n");
            terminal_writestring("  date              - exibe data e hora atual\n");
            terminal_writestring("  meminfo           - uso do heap\n");
        }
        else if (strcmpi(buffer, "clear") == 0) {
            terminal_clear();
        }
        else if (strcmpi(buffer, "reboot") == 0) {
            terminal_writestring("Reiniciando...\n");
            reboot();
        }
        else if (strcmpi(buffer, "poweroff") == 0) {
            terminal_writestring("Desligando...\n");
            poweroff();
        }
        else if (strcmpi(buffer, "info") == 0) {
            terminal_writestring("FreeRootSDOS v0.3 - Kernel em C com bootloader\n");
            terminal_writestring("Modo protegido 32-bit, IDT/IRQ, VGA texto, shell interativo.\n");
        }
        else if (startswith(buffer, "log ")) {
            cmd_log(startswith(buffer, "log "));
        }
        else if (startswith(buffer, "color ")) {
            const char *arg = startswith(buffer, "color ");
            int v = hexdigit(arg[0]);
            if (v < 0 || arg[1] != '\0')
                terminal_writestring("Uso: color <0-F>  (ex: color A)\n");
            else { terminal_set_fg((uint8_t)v); terminal_writestring("Cor do texto alterada.\n"); }
        }
        else if (startswith(buffer, "bgcolor ")) {
            const char *arg = startswith(buffer, "bgcolor ");
            int v = hexdigit(arg[0]);
            if (v < 0 || arg[1] != '\0')
                terminal_writestring("Uso: bgcolor <0-F>  (ex: bgcolor 1)\n");
            else { terminal_set_bg((uint8_t)v); terminal_clear(); terminal_writestring("Cor de fundo alterada.\n"); }
        }
        else if (strcmpi(buffer, "uptime") == 0) {
            uint32_t ms = timer_get_ticks();  // 1 tick = 1 ms (PIT a 1000 Hz)
            uint32_t s  = ms / 1000;
            uint32_t h  = s / 3600;
            uint32_t m  = (s % 3600) / 60;
            terminal_writestring("Uptime: ");
            print_dec2(h);      terminal_putchar(':');
            print_dec2(m);      terminal_putchar(':');
            print_dec2(s % 60); terminal_putchar('\n');
        }
        else if (startswith(buffer, "sleep ")) {
            const char *arg = startswith(buffer, "sleep ");
            uint32_t ms = 0;
            while (*arg >= '0' && *arg <= '9') ms = ms * 10 + (*arg++ - '0');
            if (ms == 0 || *arg != '\0') {
                terminal_writestring("Uso: sleep <ms>  (ex: sleep 1000)\n");
            } else {
                timer_sleep(ms);
                terminal_writestring("Pronto.\n");
            }
        }
        else if (strcmpi(buffer, "date") == 0) {
            // Le RTC: seg=0x00 min=0x02 hora=0x04 dia=0x07 mes=0x08 ano=0x09
            uint8_t sec = bcd_to_bin(rtc_read(0x00));
            uint8_t min = bcd_to_bin(rtc_read(0x02));
            uint8_t hr  = bcd_to_bin(rtc_read(0x04));
            uint8_t day = bcd_to_bin(rtc_read(0x07));
            uint8_t mon = bcd_to_bin(rtc_read(0x08));
            uint8_t yr  = bcd_to_bin(rtc_read(0x09));
            print_dec2(day);  terminal_putchar('/');
            print_dec2(mon);  terminal_putchar('/');
            terminal_writestring("20");
            print_dec2(yr);   terminal_writestring("  ");
            print_dec2(hr);   terminal_putchar(':');
            print_dec2(min);  terminal_putchar(':');
            print_dec2(sec);  terminal_putchar('\n');
        }
        else if (strcmpi(buffer, "meminfo") == 0) {
            uint32_t free_bytes  = kmalloc_free();
            uint32_t total_bytes = 64u * 1024u;
            uint32_t used_bytes  = total_bytes - free_bytes;
            terminal_writestring("Heap total: "); print_kb(total_bytes); terminal_putchar('\n');
            terminal_writestring("Heap usado: "); print_kb(used_bytes);  terminal_putchar('\n');
            terminal_writestring("Heap livre: "); print_kb(free_bytes);  terminal_putchar('\n');
        }
        else if (buffer[0] != '\0') {
            terminal_writestring("Comando desconhecido: ");
            terminal_writestring(buffer);
            terminal_putchar('\n');
        }
    }
}
