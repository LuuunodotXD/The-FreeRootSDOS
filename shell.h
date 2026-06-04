#ifndef SHELL_H
#define SHELL_H

void shell_init(void);
void shell_run(void);
void reboot(void);

// Executa uma linha de comando (suporta múltiplos separados por ';').
// Pode ser chamado externamente pelo terminal gráfico Balloon.
void parse_and_execute(char *line);

#endif
