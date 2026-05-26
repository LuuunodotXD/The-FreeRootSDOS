// shell.c
#include "terminal.h"
#include "keyboard.h"
#include "shell.h"
#include "io.h"

// ----------------------------------------------------------------
// Utilitários internos
// ----------------------------------------------------------------

static int strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

static const char *startswith(const char *str, const char *prefix) {
    while (*prefix)
        if (*str++ != *prefix++) return 0;
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
// RTC para uptime
// ----------------------------------------------------------------

static uint8_t bcd_to_bin(uint8_t v) { return (v >> 4) * 10 + (v & 0xF); }

static uint8_t rtc_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static unsigned int rtc_seconds(void) {
    while (rtc_read(0x0A) & 0x80);
    return bcd_to_bin(rtc_read(0x04)) * 3600u
         + bcd_to_bin(rtc_read(0x02)) * 60u
         + bcd_to_bin(rtc_read(0x00));
}

static unsigned int boot_seconds = 0;

void shell_init(void) {
    boot_seconds = rtc_seconds();
}

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
// log com cor opcional
//
// Sintaxe:
//   log /X\ texto   — imprime "texto" na cor X (hex 0–F)
//   log texto       — imprime na cor de foreground atual
//
// A cor é restaurada após a impressão.
// ----------------------------------------------------------------

static void cmd_log(const char *arg) {
    // Detecta o padrao /X\ (barra, hex, contrabarra)
    if (arg[0] == '/' && hexdigit(arg[1]) >= 0 && arg[2] == '\\') {
        uint8_t old_fg = terminal_get_fg();
        terminal_set_fg((uint8_t)hexdigit(arg[1]));

        // O texto começa após /X\, pula espaço opcional
        const char *text = arg + 3;
        if (*text == ' ') text++;

        terminal_writestring(text);
        terminal_putchar('\n');
        terminal_set_fg(old_fg);   // restaura cor original
    } else {
        // Sem especificador de cor — usa o foreground atual
        terminal_writestring(arg);
        terminal_putchar('\n');
    }
}

// ----------------------------------------------------------------
// Shell principal
// ----------------------------------------------------------------

void shell_run(void) {
    char buffer[256];
    int pos;

    while (1) {
        terminal_writestring("\n> ");
        pos = 0;

        while (1) {
            char c = getchar();
            if (c == '\n') {
                buffer[pos] = '\0';
                terminal_putchar('\n');
                break;
            } else if (c == '\b') {
                if (pos > 0) { pos--; terminal_putchar('\b'); }
            } else if (c >= ' ' && c <= '~' && pos < 255) {
                buffer[pos++] = c;
                terminal_putchar(c);
            }
        }

        if (strcmp(buffer, "help") == 0) {
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
        }
        else if (strcmp(buffer, "clear") == 0) {
            terminal_clear();
        }
        else if (strcmp(buffer, "reboot") == 0) {
            terminal_writestring("Reiniciando...\n");
            reboot();
        }
        else if (strcmp(buffer, "poweroff") == 0) {
            terminal_writestring("Desligando...\n");
            poweroff();
        }
        else if (strcmp(buffer, "info") == 0) {
            terminal_writestring("FreeRootSDOS v0.2 - Kernel em C com bootloader\n");
            terminal_writestring("Modo protegido 32-bit, IDT/IRQ, VGA texto, shell interativa.\n");
        }
        else if (startswith(buffer, "log ")) {
            cmd_log(startswith(buffer, "log "));
        }
        else if (startswith(buffer, "color ")) {
            const char *arg = startswith(buffer, "color ");
            int v = hexdigit(arg[0]);
            if (v < 0 || arg[1] != '\0')
                terminal_writestring("Uso: color <0-F>  (ex: color A)\n");
            else {
                terminal_set_fg((uint8_t)v);
                terminal_writestring("Cor do texto alterada.\n");
            }
        }
        else if (startswith(buffer, "bgcolor ")) {
            const char *arg = startswith(buffer, "bgcolor ");
            int v = hexdigit(arg[0]);
            if (v < 0 || arg[1] != '\0')
                terminal_writestring("Uso: bgcolor <0-F>  (ex: bgcolor 1)\n");
            else {
                terminal_set_bg((uint8_t)v);
                terminal_clear();
                terminal_writestring("Cor de fundo alterada.\n");
            }
        }
        else if (strcmp(buffer, "uptime") == 0) {
            unsigned int now  = rtc_seconds();
            unsigned int diff = (now >= boot_seconds)
                              ? now - boot_seconds
                              : now + 86400u - boot_seconds;
            terminal_writestring("Uptime: ");
            print_dec2(diff / 3600);        terminal_putchar(':');
            print_dec2((diff % 3600) / 60); terminal_putchar(':');
            print_dec2(diff % 60);          terminal_putchar('\n');
        }
        else if (strcmp(buffer, "") != 0) {
            terminal_writestring("Comando desconhecido: ");
            terminal_writestring(buffer);
            terminal_putchar('\n');
        }
    }
}
