#ifndef FS_H
#define FS_H

#include <stdint.h>

#define FS_NAME_LEN  8    // max chars no nome
#define FS_EXT_LEN   3    // max chars na extensao
#define FS_MAX_FILES 32

typedef struct {
    char     name[FS_NAME_LEN + 1];
    char     ext [FS_EXT_LEN  + 1];
    char    *data;    // conteudo alocado via kmalloc (0 se vazio)
    uint32_t size;    // tamanho do conteudo em bytes
    uint8_t  used;    // 1 = entrada valida
} fs_file_t;

// Inicializa o FS (zera a tabela)
void        fs_init(void);

// Retorna ponteiro para a tabela interna (para o shell iterar em dir)
fs_file_t  *fs_table(void);
int         fs_max(void);   // retorna FS_MAX_FILES

// Cria ou sobrescreve arquivo. content pode ser 0 (arquivo vazio).
// Retorna 0 ok, -1 tabela cheia, -2 nome invalido
int  fs_write(const char *name, const char *ext,
              const char *content, uint32_t size);

// Retorna ponteiro direto para o conteudo (nao aloca copia).
// Retorna 0 se nao existe ou vazio.
const char *fs_read(const char *name, const char *ext);

// Remove arquivo. Retorna 0 ok, -1 nao encontrado.
int  fs_delete(const char *name, const char *ext);

// Renomeia. Retorna 0 ok, -1 origem nao existe, -2 destino ja existe.
int  fs_rename(const char *name, const char *ext,
               const char *newname, const char *newext);

// Apaga todos os arquivos.
void fs_format(void);

// Utilitario: separa "readme.txt" em name="readme" ext="txt"
// Se nao houver ponto, ext fica vazio.
void fs_split(const char *input, char *name, char *ext);

#endif
