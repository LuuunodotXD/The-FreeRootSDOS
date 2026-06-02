#include <stdint.h>
#include "fs_disk.h"
#include "disk.h"
#include "kmalloc.h"
#include "terminal.h"

// ------------------------------------------------------------
// Funções auxiliares de memória
// ------------------------------------------------------------
static void *memcpy(void *dest, const void *src, int n) {
    char *d = dest;
    const char *s = src;
    while (n--) *d++ = *s++;
    return dest;
}

static void *memset(void *s, int c, int n) {
    char *p = s;
    while (n--) *p++ = (char)c;
    return s;
}

// ------------------------------------------------------------
// Utilitários de string (essenciais)
// ------------------------------------------------------------
static int strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static int strcmpi(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return *a - *b;
}

static void strcpy_lower(char *dst, const char *src, int max) {
    int i = 0;
    while (i < max-1 && src[i]) {
        char c = src[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        dst[i++] = c;
    }
    dst[i] = '\0';
}

static void strcpy_upper(char *dst, const char *src, int max) {
    int i = 0;
    while (i < max-1 && src[i]) {
        char c = src[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        dst[i++] = c;
    }
    dst[i] = '\0';
}

static int valid_name(const char *s, int maxlen) {
    if (!s || !s[0] || strlen(s) > maxlen) return 0;
    // Permite "." e ".."? Não, são proibidos.
    if ((s[0] == '.' && s[1] == '\0') || (s[0] == '.' && s[1] == '.' && s[2] == '\0'))
        return 0;
    for (int i = 0; s[i]; i++) {
        char c = s[i];
        // Permite ponto apenas no primeiro caractere
        if (i == 0 && c == '.') continue;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return 0;
    }
    return 1;
}

// ------------------------------------------------------------
// Estruturas globais
// ------------------------------------------------------------
static fsd_entry_t table[FSD_MAX];
static uint8_t cwd = FSD_ROOT;
static uint8_t sec_bitmap[(FSD_DATA_SECTORS + 7) / 8];

// ------------------------------------------------------------
// Busca na tabela
// ------------------------------------------------------------
static int find_entry(const char *name, const char *ext, uint8_t parent) {
    for (int i = 0; i < FSD_MAX; i++) {
        if (table[i].type == FSD_FREE) continue;
        if (table[i].parent != parent) continue;
        if (strcmpi(table[i].name, name) != 0) continue;
        if (table[i].type == FSD_FILE &&
            strcmpi(table[i].ext, ext ? ext : "") != 0) continue;
        return i;
    }
    return -1;
}

static int free_slot(void) {
    for (int i = 1; i < FSD_MAX; i++)
        if (table[i].type == FSD_FREE) return i;
    return -1;
}

// ------------------------------------------------------------
// Bitmap de setores
// ------------------------------------------------------------
static int sec_used(int s) {
    return (sec_bitmap[s / 8] >> (s % 8)) & 1;
}

static void sec_set(int s, int used) {
    if (used) sec_bitmap[s / 8] |= (1 << (s % 8));
    else      sec_bitmap[s / 8] &= ~(1 << (s % 8));
}

static int sec_alloc(int n) {
    int start = 0, count = 0;
    for (int i = 0; i < FSD_DATA_SECTORS; i++) {
        if (!sec_used(i)) {
            if (count == 0) start = i;
            if (++count == n) {
                for (int j = start; j < start + n; j++) sec_set(j, 1);
                return start;
            }
        } else count = 0;
    }
    return -1;
}

static void sec_free(int start, int n) {
    for (int i = start; i < start + n; i++) sec_set(i, 0);
}

// ------------------------------------------------------------
// Persistência
// ------------------------------------------------------------
static void save_super(void) {
    uint8_t buf[512] = {0};
    buf[0] = (FSD_MAGIC >> 0) & 0xFF;
    buf[1] = (FSD_MAGIC >> 8) & 0xFF;
    buf[2] = (FSD_MAGIC >> 16) & 0xFF;
    buf[3] = (FSD_MAGIC >> 24) & 0xFF;
    int bm_size = (FSD_DATA_SECTORS + 7) / 8;
    for (int i = 0; i < bm_size; i++) buf[4 + i] = sec_bitmap[i];
    disk_write(FSD_SECTOR_SB, buf);
}

static int load_super(void) {
    uint8_t buf[512];
    if (disk_read(FSD_SECTOR_SB, buf) != 0) return -1;
    uint32_t magic = buf[0] | (buf[1]<<8) | (buf[2]<<16) | ((uint32_t)buf[3]<<24);
    if (magic != FSD_MAGIC) return -1;
    int bm_size = (FSD_DATA_SECTORS + 7) / 8;
    for (int i = 0; i < bm_size; i++) sec_bitmap[i] = buf[4 + i];
    return 0;
}

static void save_table(void) {
    uint8_t buf[512];
    for (int s = 0; s < 4; s++) {
        memset(buf, 0, 512);
        for (int e = 0; e < 16; e++) {
            fsd_entry_t *en = &table[s*16 + e];
            uint8_t *p = buf + e*32;
            p[0] = en->type;
            p[1] = en->parent;
            memcpy(p+2, en->name, 9);
            memcpy(p+11, en->ext, 4);
            p[15] = (en->size >> 0) & 0xFF;
            p[16] = (en->size >> 8) & 0xFF;
            p[17] = (en->size >> 16) & 0xFF;
            p[18] = (en->size >> 24) & 0xFF;
            p[19] = en->start_sec & 0xFF;
            p[20] = (en->start_sec >> 8) & 0xFF;
            p[21] = en->num_secs & 0xFF;
            p[22] = (en->num_secs >> 8) & 0xFF;
        }
        disk_write(FSD_SECTOR_TABLE + s, buf);
    }
}

static void load_table(void) {
    uint8_t buf[512];
    for (int s = 0; s < 4; s++) {
        if (disk_read(FSD_SECTOR_TABLE + s, buf) != 0) continue;
        for (int e = 0; e < 16; e++) {
            fsd_entry_t *en = &table[s*16 + e];
            uint8_t *p = buf + e*32;
            en->type = p[0];
            en->parent = p[1];
            memcpy(en->name, p+2, 9);
            memcpy(en->ext, p+11, 4);
            en->size = (uint32_t)p[15] | ((uint32_t)p[16]<<8) |
                       ((uint32_t)p[17]<<16) | ((uint32_t)p[18]<<24);
            en->start_sec = (uint16_t)p[19] | ((uint16_t)p[20]<<8);
            en->num_secs  = (uint16_t)p[21] | ((uint16_t)p[22]<<8);
        }
    }
}

// ------------------------------------------------------------
// API pública
// ------------------------------------------------------------
void fsd_init(void) {
    for (int i = 0; i < FSD_MAX; i++) table[i].type = FSD_FREE;
    memset(sec_bitmap, 0, sizeof(sec_bitmap));
    if (load_super() == 0) {
        load_table();
    } else {
        table[FSD_ROOT].type = FSD_DIR;
        table[FSD_ROOT].parent = FSD_ROOT;
        table[FSD_ROOT].name[0] = '\0';
        save_super();
        save_table();
    }
    cwd = FSD_ROOT;
}

uint8_t fsd_cwd(void) { return cwd; }
void    fsd_set_cwd(uint8_t idx) { cwd = idx; }
const char *fsd_cwd_name(void) { return table[cwd].name; }
fsd_entry_t *fsd_table(void) { return table; }
int fsd_max(void) { return FSD_MAX; }

void fsd_split(const char *input, char *name, char *ext) {
    const char *dot = 0;
    for (const char *p = input; *p; p++) if (*p == '.') dot = p;
    if (dot && dot != input) {
        int nlen = dot - input;
        if (nlen > FSD_NAME_LEN) nlen = FSD_NAME_LEN;
        char tmp[FSD_NAME_LEN+1];
        memcpy(tmp, input, nlen);
        tmp[nlen] = '\0';
        strcpy_lower(name, tmp, FSD_NAME_LEN+1);
        strcpy_lower(ext, dot+1, FSD_EXT_LEN+1);
    } else {
        strcpy_lower(name, input, FSD_NAME_LEN+1);
        ext[0] = '\0';
    }
}

int fsd_cd(const char *name) {
    if (strcmpi(name, "..") == 0) {
        cwd = table[cwd].parent;
        return 0;
    }
    int idx = find_entry(name, "", cwd);
    if (idx < 0 || table[idx].type != FSD_DIR) return -1;
    cwd = (uint8_t)idx;
    return 0;
}

int fsd_mkdir(const char *name) {
    if (!valid_name(name, FSD_NAME_LEN)) return -2;
    if (find_entry(name, "", cwd) >= 0) return -3;
    int idx = free_slot();
    if (idx < 0) return -1;
    table[idx].type = FSD_DIR;
    table[idx].parent = cwd;
    table[idx].size = 0;
    table[idx].start_sec = 0;
    table[idx].num_secs = 0;
    table[idx].ext[0] = '\0';
    strcpy_upper(table[idx].name, name, FSD_NAME_LEN+1);
    save_table();
    return idx;
}

int fsd_write_in(uint8_t dir, const char *name, const char *ext,
                 const char *content, uint32_t size) {
    if (!valid_name(name, FSD_NAME_LEN)) return -2;
    if (ext && ext[0] && !valid_name(ext, FSD_EXT_LEN)) return -2;

    int idx = find_entry(name, ext, dir);
    if (idx >= 0) {
        if (table[idx].num_secs > 0)
            sec_free(table[idx].start_sec, table[idx].num_secs);
        table[idx].size = 0;
        table[idx].num_secs = 0;
        table[idx].start_sec = 0;
    } else {
        idx = free_slot();
        if (idx < 0) return -1;
        table[idx].type = FSD_FILE;
        table[idx].parent = dir;
        table[idx].size = 0;
        table[idx].num_secs = 0;
        table[idx].start_sec = 0;
        strcpy_lower(table[idx].name, name, FSD_NAME_LEN+1);
        strcpy_lower(table[idx].ext, ext ? ext : "", FSD_EXT_LEN+1);
    }

    if (content && size > 0) {
        int nsecs = (size + 511) / 512;
        int start = sec_alloc(nsecs);
        if (start < 0) return -1;
        uint8_t buf[512];
        uint32_t written = 0;
        for (int s = 0; s < nsecs; s++) {
            memset(buf, 0, 512);
            for (int i = 0; i < 512 && written < size; i++)
                buf[i] = content[written++];
            if (disk_write(FSD_SECTOR_DATA + start + s, buf) != 0) {
                sec_free(start, nsecs);
                return -1;
            }
        }
        table[idx].size = size;
        table[idx].start_sec = (uint16_t)start;
        table[idx].num_secs = (uint16_t)nsecs;
    }

    save_super();
    save_table();
    return 0;
}

int fsd_write(const char *name, const char *ext,
              const char *content, uint32_t size) {
    return fsd_write_in(cwd, name, ext, content, size);
}

const char *fsd_read(const char *name, const char *ext) {
    int idx = find_entry(name, ext, cwd);
    if (idx < 0 || table[idx].type != FSD_FILE || table[idx].size == 0) return 0;
    uint32_t size = table[idx].size;
    char *buf = (char*)kmalloc(size + 1);
    if (!buf) return 0;
    uint8_t sector[512];
    uint32_t pos = 0;
    for (int s = 0; s < table[idx].num_secs; s++) {
        if (disk_read(FSD_SECTOR_DATA + table[idx].start_sec + s, sector) != 0) {
            kfree(buf);
            return 0;
        }
        for (int i = 0; i < 512 && pos < size; i++)
            buf[pos++] = sector[i];
    }
    buf[pos] = '\0';
    return buf; // quem chama deve kfree()
}


const char *fsd_read_idx(int idx) {
    if (idx < 0 || idx >= FSD_MAX) return 0;
    if (table[idx].type != FSD_FILE || table[idx].size == 0) return 0;
    uint32_t size = table[idx].size;
    char *buf = (char*)kmalloc(size + 1);
    if (!buf) return 0;
    uint8_t sector[512];
    uint32_t pos = 0;
    for (int s = 0; s < table[idx].num_secs; s++) {
        if (disk_read(FSD_SECTOR_DATA + table[idx].start_sec + s, sector) != 0) {
            kfree(buf); return 0;
        }
        for (int i = 0; i < 512 && pos < size; i++)
            buf[pos++] = sector[i];
    }
    buf[pos] = '\0';
    return buf;
}

void fsd_kfree_read(const char *p) {
    if (p) kfree((void*)p);
}

int fsd_delete(const char *name, const char *ext, int is_dir) {
    if (is_dir) {
        int idx = find_entry(name, "", cwd);
        if (idx < 0 || table[idx].type != FSD_DIR || idx == FSD_ROOT) return -1;
        for (int i = 1; i < FSD_MAX; i++) {
            if (table[i].type != FSD_FREE && table[i].parent == (uint8_t)idx) {
                if (table[i].num_secs > 0)
                    sec_free(table[i].start_sec, table[i].num_secs);
                table[i].type = FSD_FREE;
            }
        }
        if (cwd == (uint8_t)idx) cwd = FSD_ROOT;
        table[idx].type = FSD_FREE;
    } else {
        int idx = find_entry(name, ext, cwd);
        if (idx < 0 || table[idx].type != FSD_FILE) return -1;
        if (table[idx].num_secs > 0)
            sec_free(table[idx].start_sec, table[idx].num_secs);
        table[idx].type = FSD_FREE;
    }
    save_super();
    save_table();
    return 0;
}

int fsd_rename(const char *name, const char *ext,
               const char *newname, const char *newext) {
    if (!valid_name(newname, FSD_NAME_LEN)) return -1;
    int src = find_entry(name, ext, cwd);
    if (src < 0 || table[src].type != FSD_FILE) return -1;
    if (find_entry(newname, newext, cwd) >= 0) return -2;
    strcpy_lower(table[src].name, newname, FSD_NAME_LEN+1);
    strcpy_lower(table[src].ext, newext ? newext : "", FSD_EXT_LEN+1);
    save_table();
    return 0;
}

void fsd_format(void) {
    for (int i = 1; i < FSD_MAX; i++) {
        if (table[i].type != FSD_FREE && table[i].num_secs > 0)
            sec_free(table[i].start_sec, table[i].num_secs);
        table[i].type = FSD_FREE;
    }
    memset(sec_bitmap, 0, sizeof(sec_bitmap));
    cwd = FSD_ROOT;
    save_super();
    save_table();
}
