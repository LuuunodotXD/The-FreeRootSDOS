#ifndef FS_H
#define FS_H

#include <stdint.h>

#define FS_NAME_LEN   8
#define FS_EXT_LEN    3
#define FS_MAX        64   // total de entradas (arquivos + diretórios)
#define FS_ROOT       0    // índice da raiz

#define FS_FREE  0
#define FS_FILE  1
#define FS_DIR   2

typedef struct {
    uint8_t  type;                   // FS_FREE / FS_FILE / FS_DIR
    uint8_t  parent;                 // índice do diretório pai (0 = raiz)
    char     name[FS_NAME_LEN + 1]; // minúsculo p/ arquivos, MAIÚSCULO p/ dirs
    char     ext [FS_EXT_LEN  + 1]; // vazio para dirs
    char    *data;                   // conteúdo (kmalloc), null para dirs
    uint32_t size;
} fs_entry_t;

// Inicializa o FS
void        fs_init(void);

// Navegação
uint8_t     fs_cwd(void);
void        fs_set_cwd(uint8_t idx);            // índice do dir corrente
const char *fs_cwd_name(void);       // nome do dir corrente ("" se raiz)
int         fs_cd(const char *name); // 0 ok, -1 não existe

// Operações
int  fs_mkdir (const char *name);
int  fs_write (const char *name, const char *ext,
               const char *content, uint32_t size);
int  fs_write_in(uint8_t dir, const char *name, const char *ext,
                 const char *content, uint32_t size); // grava em dir específico
const char *fs_read(const char *name, const char *ext);
int  fs_delete(const char *name, const char *ext, int is_dir);
int  fs_rename(const char *name,    const char *ext,
               const char *newname, const char *newext);
void fs_format(void);

// Acesso à tabela (para dir e copy no shell)
fs_entry_t *fs_table(void);
int         fs_max(void);

// Utilitário: separa "readme.txt" → name="readme" ext="txt"
void fs_split(const char *input, char *name, char *ext);

#endif
