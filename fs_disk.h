#ifndef FS_DISK_H
#define FS_DISK_H

#include <stdint.h>

// ----------------------------------------------------------------
// Layout do disco (160 setores × 512 bytes = 80KB)
//
//  Setor  0       bootloader
//  Setores 1-42   kernel
//  Setor  43      superbloco
//  Setores 44-47  tabela de entradas (64 × 32 bytes = 2048 bytes)
//  Setores 48-159 área de dados (112 setores = 57.344 bytes)
// ----------------------------------------------------------------

#define FSD_MAGIC        0x46524453u  // "FRDS"
#define FSD_MAX          64
#define FSD_ROOT         0
#define FSD_NAME_LEN     8
#define FSD_EXT_LEN      3

#define FSD_FREE  0
#define FSD_FILE  1
#define FSD_DIR   2

#define FSD_SECTOR_SB      81
#define FSD_SECTOR_TABLE   82
#define FSD_SECTOR_DATA    86
#define FSD_DATA_SECTORS   74   // 160 - 86

// Entrada na tabela (32 bytes, packed)
typedef struct {
    uint8_t  type;           // FSD_FREE / FSD_FILE / FSD_DIR
    uint8_t  parent;         // índice do dir pai
    char     name[9];        // lowercase para arquivos, UPPER para dirs
    char     ext[4];         // lowercase, vazio para dirs
    uint32_t size;           // tamanho em bytes
    uint8_t  start_sec;      // setor de dados inicial (relativo a FSD_SECTOR_DATA)
    uint8_t  num_secs;       // quantos setores usa
    uint8_t  pad[6];         // padding para 32 bytes
} __attribute__((packed)) fsd_entry_t;

// Inicializa e carrega do disco (cria superbloco se não existir)
void        fsd_init(void);

// Navegação
uint8_t     fsd_cwd(void);
const char *fsd_cwd_name(void);
int         fsd_cd(const char *name);

// Operações
int         fsd_mkdir(const char *name);
int         fsd_write(const char *name, const char *ext,
                      const char *content, uint32_t size);
int         fsd_write_in(uint8_t dir, const char *name, const char *ext,
                         const char *content, uint32_t size);
const char *fsd_read(const char *name, const char *ext);
int         fsd_delete(const char *name, const char *ext, int is_dir);
int         fsd_rename(const char *name,    const char *ext,
                       const char *newname, const char *newext);
void        fsd_format(void);

// Acesso à tabela (para dir no shell)
fsd_entry_t *fsd_table(void);
int          fsd_max(void);

// Utilitário
void fsd_split(const char *input, char *name, char *ext);

#endif
