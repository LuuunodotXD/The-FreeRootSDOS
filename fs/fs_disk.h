#ifndef FS_DISK_H
#define FS_DISK_H

#include <stdint.h>

extern uint32_t fsd_base_lba;

#define FSD_SECTOR_SB      (fsd_base_lba + 0)
#define FSD_SECTOR_TABLE   (fsd_base_lba + 1)
#define FSD_SECTOR_DATA    (fsd_base_lba + 17)
#define FSD_DATA_SECTORS   914

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
void        fsd_init_at(uint32_t lba);   // ← novo
uint8_t     fsd_cwd(void);
void        fsd_set_cwd(uint8_t idx);
const char *fsd_cwd_name(void);
void        fsd_cwd_path(char *buf, int maxlen);
int         fsd_cd(const char *name);
int         fsd_mkdir(const char *name);
int         fsd_write(const char *name, const char *ext,
                      const char *content, uint32_t size);
int         fsd_write_in(uint8_t dir, const char *name, const char *ext,
                         const char *content, uint32_t size);
const char *fsd_read(const char *name, const char *ext);
const char *fsd_read_idx(int idx);
void        fsd_kfree_read(const char *p);
int         fsd_delete(const char *name, const char *ext, int is_dir);
int         fsd_rename(const char *name, const char *ext,
                       const char *newname, const char *newext);
void        fsd_format(void);
fsd_entry_t *fsd_table(void);
int         fsd_max(void);
void        fsd_split(const char *input, char *name, char *ext);

#endif
