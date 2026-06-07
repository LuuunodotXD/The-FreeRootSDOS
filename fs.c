// fs.c
#include <stdint.h>
#include "fs.h"
#include "kmalloc.h"

static fs_entry_t table[FS_MAX];
static uint8_t    cwd = FS_ROOT;

// ----------------------------------------------------------------
// Utilitários internos
// ----------------------------------------------------------------

static int fs_strlen(const char *s) {
    int n = 0; while (s[n]) n++; return n;
}

static int fs_strcmpi(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return *a - *b;
}

static void fs_normcpy_lower(char *dst, const char *src, int max) {
    int i = 0;
    while (i < max - 1 && src[i]) {
        char c = src[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        dst[i++] = c;
    }
    dst[i] = '\0';
}

static void fs_normcpy_upper(char *dst, const char *src, int max) {
    int i = 0;
    while (i < max - 1 && src[i]) {
        char c = src[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        dst[i++] = c;
    }
    dst[i] = '\0';
}

static int fs_valid(const char *s, int maxlen) {
    if (!s || !s[0] || fs_strlen(s) > maxlen) return 0;
    // Nomes especiais "." e ".." não são permitidos
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

// Busca por nome+ext no diretório pai especificado
static int fs_find(const char *name, const char *ext, uint8_t parent) {
    for (int i = 0; i < FS_MAX; i++) {
        if (table[i].type == FS_FREE) continue;
        if (table[i].parent != parent) continue;
        if (fs_strcmpi(table[i].name, name) != 0) continue;
        if (table[i].type == FS_FILE &&
            fs_strcmpi(table[i].ext, ext ? ext : "") != 0) continue;
        return i;
    }
    return -1;
}

static int fs_slot(void) {
    for (int i = 1; i < FS_MAX; i++)
        if (table[i].type == FS_FREE) return i;
    return -1;
}

// ----------------------------------------------------------------
// API pública
// ----------------------------------------------------------------

void fs_init(void) {
    for (int i = 0; i < FS_MAX; i++) {
        table[i].type    = FS_FREE;
        table[i].parent  = 0;
        table[i].name[0] = '\0';
        table[i].ext[0]  = '\0';
        table[i].data    = 0;
        table[i].size    = 0;
    }
    // Entrada 0 = raiz (sempre existe)
    table[FS_ROOT].type   = FS_DIR;
    table[FS_ROOT].parent = FS_ROOT;
    table[FS_ROOT].name[0] = '\0';   // raiz não tem nome exibido
    cwd = FS_ROOT;
}

uint8_t     fs_cwd(void)      { return cwd; }
void        fs_set_cwd(uint8_t idx) { cwd = idx; }
const char *fs_cwd_name(void) { return table[cwd].name; }
fs_entry_t *fs_table(void)    { return table; }
int         fs_max(void)      { return FS_MAX; }

void fs_split(const char *input, char *name, char *ext) {
    // Encontra o último ponto
    const char *dot = 0;
    for (const char *p = input; *p; p++)
        if (*p == '.') dot = p;

    if (dot && dot != input) {
        int nlen = dot - input;
        if (nlen > FS_NAME_LEN) nlen = FS_NAME_LEN;
        char tmp[FS_NAME_LEN + 1];
        for (int i = 0; i < nlen; i++) tmp[i] = input[i];
        tmp[nlen] = '\0';
        fs_normcpy_lower(name, tmp, FS_NAME_LEN + 1);
        fs_normcpy_lower(ext, dot + 1, FS_EXT_LEN + 1);
    } else {
        fs_normcpy_lower(name, input, FS_NAME_LEN + 1);
        ext[0] = '\0';
    }
}

int fs_cd(const char *name) {
    if (fs_strcmpi(name, "..") == 0) {
        if (cwd != FS_ROOT) cwd = table[cwd].parent;
        return 0;
    }
    int idx = fs_find(name, "", cwd);              // ← cwd, não FS_ROOT
    if (idx < 0 || table[idx].type != FS_DIR) return -1;
    cwd = (uint8_t)idx;
    return 0;
}

int fs_mkdir(const char *name) {
    if (!fs_valid(name, FS_NAME_LEN)) return -2;
    if (fs_find(name, "", cwd) >= 0) return -3;   // ← cwd
    int idx = fs_slot();
    if (idx < 0) return -1;
    table[idx].type   = FS_DIR;
    table[idx].parent = cwd;                       // ← cwd, não FS_ROOT
    table[idx].data   = 0;
    table[idx].size   = 0;
    table[idx].ext[0] = '\0';
    fs_normcpy_upper(table[idx].name, name, FS_NAME_LEN + 1);
    return idx;
}

void fs_cwd_path(char *buf, int maxlen) {
    if (cwd == FS_ROOT) { buf[0] = '\0'; return; }

    uint8_t stack[FS_MAX];
    int depth = 0;
    uint8_t cur = cwd;

    // Sobe até a raiz empilhando cada nível
    while (depth < FS_MAX) {
        stack[depth++] = cur;
        uint8_t par = table[cur].parent;
        if (par == cur || par == FS_ROOT) break;  // ← condição correta
        cur = par;
    }

    // Monta o caminho de cima para baixo
    int pos = 0;
    for (int i = depth - 1; i >= 0 && pos < maxlen - 2; i--) {
        buf[pos++] = '/';
        const char *n = table[stack[i]].name;
        for (int j = 0; n[j] && pos < maxlen - 2; j++)
            buf[pos++] = n[j];
    }
    buf[pos] = '\0';
}

// Grava arquivo no diretório 'dir'
int fs_write_in(uint8_t dir, const char *name, const char *ext,
                const char *content, uint32_t size) {
    if (!fs_valid(name, FS_NAME_LEN)) return -2;
    if (ext && ext[0] && !fs_valid(ext, FS_EXT_LEN)) return -2;

    int idx = fs_find(name, ext, dir);
    if (idx < 0) {
        idx = fs_slot();
        if (idx < 0) return -1;
        table[idx].type   = FS_FILE;
        table[idx].parent = dir;
        table[idx].data   = 0;
        table[idx].size   = 0;
        fs_normcpy_lower(table[idx].name, name, FS_NAME_LEN + 1);
        fs_normcpy_lower(table[idx].ext,  ext ? ext : "", FS_EXT_LEN + 1);
    }
    if (table[idx].data) { kfree(table[idx].data); table[idx].data = 0; }
    table[idx].size = 0;

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

// Grava no diretório corrente
int fs_write(const char *name, const char *ext,
             const char *content, uint32_t size) {
    return fs_write_in(cwd, name, ext, content, size);
}

const char *fs_read(const char *name, const char *ext) {
    int idx = fs_find(name, ext ? ext : "", cwd);
    if (idx < 0 || table[idx].type != FS_FILE) return 0;
    return table[idx].data;
}

static void fs_delete_recursive(uint8_t dir_idx) {
    for (int i = 1; i < FS_MAX; i++) {
        if (table[i].type == FS_FREE) continue;
        if (table[i].parent != dir_idx) continue;
        if (table[i].type == FS_DIR)
            fs_delete_recursive((uint8_t)i);
        if (table[i].data) { kfree(table[i].data); table[i].data = 0; }
        table[i].type = FS_FREE;
        table[i].size = 0;
    }
}

int fs_delete(const char *name, const char *ext, int is_dir) {
    if (is_dir) {
        int idx = fs_find(name, "", cwd);             // ← cwd, não FS_ROOT
        if (idx < 0 || table[idx].type != FS_DIR || idx == FS_ROOT) return -1;
        fs_delete_recursive((uint8_t)idx);
        if (cwd == (uint8_t)idx) cwd = table[idx].parent;
        table[idx].type = FS_FREE;
        return 0;
    }
}

int fs_rename(const char *name,    const char *ext,
              const char *newname, const char *newext) {
    if (!fs_valid(newname, FS_NAME_LEN)) return -1;
    int src = fs_find(name, ext ? ext : "", cwd);
    if (src < 0 || table[src].type != FS_FILE) return -1;
    if (fs_find(newname, newext ? newext : "", cwd) >= 0) return -2;
    fs_normcpy_lower(table[src].name, newname, FS_NAME_LEN + 1);
    fs_normcpy_lower(table[src].ext,  newext ? newext : "", FS_EXT_LEN + 1);
    return 0;
}

void fs_format(void) {
    for (int i = 1; i < FS_MAX; i++) {
        if (table[i].data) kfree(table[i].data);
        table[i].type = FS_FREE;
        table[i].data = 0;
        table[i].size = 0;
    }
    cwd = FS_ROOT;
}
