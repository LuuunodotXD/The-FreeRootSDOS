// fs.c -- sistema de arquivos flat em memoria
#include <stdint.h>
#include "fs.h"
#include "kmalloc.h"

static fs_file_t table[FS_MAX_FILES];

// ----------------------------------------------------------------
// Utilitarios internos
// ----------------------------------------------------------------

static int fs_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

static int fs_strlen(const char *s) {
    int n = 0; while (s[n]) n++; return n;
}

static void fs_strncpy(char *dst, const char *src, int max) {
    int i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static char fs_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

// Compara ignorando maiusculas/minusculas
static int fs_strcmpi(const char *a, const char *b) {
    while (*a && *b && fs_lower(*a) == fs_lower(*b)) { a++; b++; }
    return fs_lower(*a) - fs_lower(*b);
}

// Copia normalizada para lowercase, truncando em max chars
static void fs_normcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (i < max - 1 && src[i]) { dst[i] = fs_lower(src[i]); i++; }
    dst[i] = '\0';
}

static int fs_find(const char *name, const char *ext) {
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (!table[i].used) continue;
        if (fs_strcmpi(table[i].name, name) != 0) continue;
        if (fs_strcmpi(table[i].ext,  ext)  != 0) continue;
        return i;
    }
    return -1;
}

static int fs_slot(void) {
    for (int i = 0; i < FS_MAX_FILES; i++)
        if (!table[i].used) return i;
    return -1;
}

static int fs_valid_name(const char *name, int maxlen) {
    if (!name || !name[0]) return 0;
    int n = fs_strlen(name);
    if (n > maxlen) return 0;
    for (int i = 0; i < n; i++) {
        char c = name[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return 0;
    }
    return 1;
}

// ----------------------------------------------------------------
// API publica
// ----------------------------------------------------------------

void fs_init(void) {
    for (int i = 0; i < FS_MAX_FILES; i++) {
        table[i].used    = 0;
        table[i].data    = 0;
        table[i].size    = 0;
        table[i].name[0] = '\0';
        table[i].ext[0]  = '\0';
    }
}

fs_file_t *fs_table(void) { return table; }
int        fs_max(void)   { return FS_MAX_FILES; }

void fs_split(const char *input, char *name, char *ext) {
    // Encontra o ultimo ponto
    const char *dot = 0;
    for (const char *p = input; *p; p++)
        if (*p == '.') dot = p;

    if (dot && dot != input) {
        // Tem extensao
        int nlen = dot - input;
        if (nlen > FS_NAME_LEN) nlen = FS_NAME_LEN;
        fs_strncpy(name, input, nlen + 1);
        name[nlen] = '\0';
        fs_normcpy(name, name, FS_NAME_LEN + 1);
        fs_normcpy(ext, dot + 1, FS_EXT_LEN + 1);
    } else {
        // Sem extensao
        fs_normcpy(name, input, FS_NAME_LEN + 1);
        ext[0] = '\0';
    }
}

int fs_write(const char *name, const char *ext,
             const char *content, uint32_t size) {
    if (!fs_valid_name(name, FS_NAME_LEN)) return -2;
    if (ext && ext[0] && !fs_valid_name(ext, FS_EXT_LEN)) return -2;

    int idx = fs_find(name, ext);
    if (idx < 0) {
        idx = fs_slot();
        if (idx < 0) return -1;   // tabela cheia
        table[idx].used = 1;
        table[idx].data = 0;
        table[idx].size = 0;
        fs_normcpy(table[idx].name, name, FS_NAME_LEN + 1);
        fs_normcpy(table[idx].ext,  ext ? ext : "", FS_EXT_LEN + 1);
    }

    // Libera conteudo anterior
    if (table[idx].data) {
        kfree(table[idx].data);
        table[idx].data = 0;
        table[idx].size = 0;
    }

    if (content && size > 0) {
        char *p = (char *)kmalloc(size + 1);
        if (!p) return -1;
        for (uint32_t i = 0; i < size; i++) p[i] = content[i];
        p[size] = '\0';
        table[idx].data = p;
        table[idx].size = size;
    }
    return 0;
}

const char *fs_read(const char *name, const char *ext) {
    int idx = fs_find(name, ext ? ext : "");
    if (idx < 0 || !table[idx].data) return 0;
    return table[idx].data;
}

int fs_delete(const char *name, const char *ext) {
    int idx = fs_find(name, ext ? ext : "");
    if (idx < 0) return -1;
    if (table[idx].data) kfree(table[idx].data);
    table[idx].used = 0;
    table[idx].data = 0;
    table[idx].size = 0;
    return 0;
}

int fs_rename(const char *name,    const char *ext,
              const char *newname, const char *newext) {
    if (!fs_valid_name(newname, FS_NAME_LEN)) return -1;
    int src = fs_find(name, ext ? ext : "");
    if (src < 0) return -1;
    if (fs_find(newname, newext ? newext : "") >= 0) return -2;
    fs_normcpy(table[src].name, newname, FS_NAME_LEN + 1);
    fs_normcpy(table[src].ext,  newext ? newext : "", FS_EXT_LEN + 1);
    return 0;
}

void fs_format(void) {
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (table[i].used && table[i].data) kfree(table[i].data);
        table[i].used = 0;
        table[i].data = 0;
        table[i].size = 0;
    }
}
