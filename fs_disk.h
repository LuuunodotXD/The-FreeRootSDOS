#ifndef FS_DISK_H
#define FS_DISK_H

#include <stdint.h>

#define FSD_SECTOR_SB      161     // superbloco 128
#define FSD_SECTOR_TABLE   162     // tabela (4 setores: 129..133) 129
#define FSD_SECTOR_DATA    166     // dados a partir daqui 133
#define FSD_DATA_SECTORS   154     // até setor 319 (320-133=187)

#define FSD_MAGIC          0x46524453u
#define FSD_MAX            64
#define FSD_ROOT           0
#define FSD_NAME_LEN       8
#define FSD_EXT_LEN        3

#define FSD_FREE  0
#define FSD_FILE  1
#define FSD_DIR   2

typedef struct {
    uint8_t  type;
    uint8_t  parent;
    char     name[9];
    char     ext[4];
    uint32_t size;
    uint16_t start_sec;   // setor relativo a FSD_SECTOR_DATA
    uint16_t num_secs;
    uint8_t  pad[2];
} __attribute__((packed)) fsd_entry_t;

void        fsd_init(void);
uint8_t     fsd_cwd(void);
const char *fsd_cwd_name(void);
int         fsd_cd(const char *name);
int         fsd_mkdir(const char *name);
int         fsd_write(const char *name, const char *ext,
                      const char *content, uint32_t size);
int         fsd_write_in(uint8_t dir, const char *name, const char *ext,
                         const char *content, uint32_t size);
const char *fsd_read(const char *name, const char *ext);
int         fsd_delete(const char *name, const char *ext, int is_dir);
int         fsd_rename(const char *name, const char *ext,
                       const char *newname, const char *newext);
void        fsd_format(void);
fsd_entry_t *fsd_table(void);
int         fsd_max(void);
void        fsd_split(const char *input, char *name, char *ext);

#endif
