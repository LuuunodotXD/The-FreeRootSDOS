// shell.c
#include "terminal.h"   // para terminal_writestring, terminal_clear, terminal_putchar
#include "keyboard.h"   // para getchar()
#include "shell.h"   // opcional, mas consistente
#include "io.h"

// Implementação simples de strcmp (já que não temos libc)
static int strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a - *b;
}

void shell_run(void) {
    char buffer[256];
    int pos;

    while (1) {
        terminal_writestring("\n> ");
        pos = 0;

        // Loop de leitura de uma linha
        while (1) {
            char c = getchar();

            if (c == '\n') {               // Enter finaliza o comando
                buffer[pos] = '\0';
                terminal_putchar('\n');
                break;
            }
            else if (c == '\b' && pos > 0) { // Backspace
                pos--;
                terminal_putchar('\b');
            }
            else if (c >= ' ' && c <= '~' && pos < 255) { // caracteres imprimíveis
                buffer[pos++] = c;
                terminal_putchar(c);
            }
        }

        // --- Processamento dos comandos ---
        if (strcmp(buffer, "help") == 0) {
            terminal_writestring("Comandos disponiveis:\n");
            terminal_writestring("  help   - mostra esta ajuda\n");
            terminal_writestring("  clear  - limpa a tela\n");
            terminal_writestring("  reboot - reinicia o sistema\n");
            terminal_writestring("  info   - mostra informacoes do SO\n");
        }
        else if (strcmp(buffer, "clear") == 0) {
            terminal_clear();
        }
        else if (strcmp(buffer, "reboot") == 0) {
            terminal_writestring("Reiniciando...\n");
            outb(0x64, 0xFE);   // comando de reset da CPU
            // Se falhar, entre em loop infinito
        for(;;);
        }
        else if (strcmp(buffer, "info") == 0) {
            terminal_writestring("FreeRootSDOS v0.1 - Kernel em C com bootloader\n");
            terminal_writestring("Modo protegido, VGA texto, shell simples.\n");
        }
        else if (strcmp(buffer, "") != 0) {
            terminal_writestring("Comando desconhecido: ");
            terminal_writestring(buffer);
            terminal_writestring("\n");
        }
    }
}
