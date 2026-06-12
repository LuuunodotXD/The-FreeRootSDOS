#ifndef SHELL_H
#define SHELL_H

void shell_init(void);
void shell_run(void);
void reboot(void);

// Executa uma linha de comando (suporta múltiplos separados por ';').
// Pode ser chamado externamente pelo terminal gráfico Balloon.
void parse_and_execute(char *line);

const char *vfs_read (const char *name, const char *ext);
int         vfs_write(const char *name, const char *ext,
                      const char *data, uint32_t size);
int        vfs_delete(const char *name, const char *ext, int is_dir);

#endif
