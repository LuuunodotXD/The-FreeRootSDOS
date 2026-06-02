// env.c -- variáveis de ambiente
#include "env.h"
#include "terminal.h"
#include "fs_disk.h"
#include "kmalloc.h"

// ----------------------------------------------------------------
// Funções auxiliares de string (evitam libc)
// ----------------------------------------------------------------
static char *strncpy(char *dest, const char *src, int n) {
    int i;
    for (i = 0; i < n && src[i]; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}

static int strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static int strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

// ----------------------------------------------------------------
// Estrutura da tabela de variáveis
// ----------------------------------------------------------------
#define ENV_MAX_VARS 32
#define ENV_NAME_LEN 16
#define ENV_VAL_LEN  64

typedef struct {
    char name[ENV_NAME_LEN + 1];
    char value[ENV_VAL_LEN + 1];
    int  used;
} env_entry_t;

static env_entry_t env_table[ENV_MAX_VARS];

// ----------------------------------------------------------------
// Busca uma variável pelo nome
// ----------------------------------------------------------------
static env_entry_t *env_find(const char *name) {
    for (int i = 0; i < ENV_MAX_VARS; i++) {
        if (env_table[i].used && strcmp(env_table[i].name, name) == 0)
            return &env_table[i];
    }
    return 0;
}

// ----------------------------------------------------------------
// Define ou atualiza uma variável (apenas na tabela em memória)
// ----------------------------------------------------------------
void env_set(const char *name, const char *value) {
    env_entry_t *e = env_find(name);
    if (!e) {
        for (int i = 0; i < ENV_MAX_VARS; i++) {
            if (!env_table[i].used) {
                e = &env_table[i];
                e->used = 1;
                break;
            }
        }
        if (!e) return; // sem espaço
    }
    strncpy(e->name, name, ENV_NAME_LEN);
    e->name[ENV_NAME_LEN] = '\0';
    strncpy(e->value, value, ENV_VAL_LEN);
    e->value[ENV_VAL_LEN] = '\0';
}

// ----------------------------------------------------------------
// Retorna o valor de uma variável (ou "" se não existir)
// ----------------------------------------------------------------
const char *env_get(const char *name) {
    env_entry_t *e = env_find(name);
    return e ? e->value : "";
}

// ----------------------------------------------------------------
// Expande $VAR e ${VAR} na string src para dst
// ----------------------------------------------------------------
void env_expand(const char *src, char *dst, int dst_len) {
    int si = 0, di = 0;
    while (src[si] && di < dst_len - 1) {
        if (src[si] == '$') {
            si++;
            int braced = (src[si] == '{');
            if (braced) si++;
            char vname[ENV_NAME_LEN + 1];
            int vi = 0;
            while (src[si] && ((src[si] >= 'A' && src[si] <= 'Z') ||
                               (src[si] >= 'a' && src[si] <= 'z') ||
                               (src[si] >= '0' && src[si] <= '9') ||
                               src[si] == '_') && vi < ENV_NAME_LEN) {
                vname[vi++] = src[si++];
            }
            vname[vi] = '\0';
            if (braced && src[si] == '}') si++;
            const char *val = env_get(vname);
            while (*val && di < dst_len - 1) dst[di++] = *val++;
        } else {
            dst[di++] = src[si++];
        }
    }
    dst[di] = '\0';
}

// ----------------------------------------------------------------
// Lista todas as variáveis (usada pelo comando set, se disponível)
// ----------------------------------------------------------------
void env_list(void) {
    for (int i = 0; i < ENV_MAX_VARS; i++) {
        if (env_table[i].used) {
            terminal_writestring(env_table[i].name);
            terminal_writestring("=");
            terminal_writestring(env_table[i].value);
            terminal_putchar('\n');
        }
    }
}

// ----------------------------------------------------------------
// Inicializa as variáveis a partir do arquivo A:.VAR/env.var
// ----------------------------------------------------------------
void env_init(void) {
    for (int i = 0; i < ENV_MAX_VARS; i++) env_table[i].used = 0;

    // Procura o diretório .VAR na raiz do A:
    fsd_entry_t *t = fsd_table();
    int var_dir = -1;
    for (int i = 1; i < fsd_max(); i++) {
        if (t[i].type == FSD_DIR && t[i].name[0] == '.' && t[i].name[1] == 'V' &&
            t[i].name[2] == 'A' && t[i].name[3] == 'R' && t[i].name[4] == '\0') {
            var_dir = i; break;
        }
    }
    if (var_dir < 0) return;

    // Procura o arquivo env.var dentro de .VAR
    int env_idx = -1;
    for (int i = 1; i < fsd_max(); i++) {
        if (t[i].type == FSD_FILE && t[i].parent == var_dir &&
            t[i].name[0] == 'e' && t[i].name[1] == 'n' && t[i].name[2] == 'v' &&
            t[i].ext[0] == 'v' && t[i].ext[1] == 'a' && t[i].ext[2] == 'r') {
            env_idx = i; break;
        }
    }
    if (env_idx < 0) return;

    const char *data = fsd_read_idx(env_idx);
    if (!data) return;

    // Parse do arquivo: linhas no formato $VAR=valor
    const char *p = data;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r') p++;
        if (*p == '\n') { p++; continue; }
        if (*p == '#') { while (*p && *p != '\n') p++; continue; }
        if (*p != '$') { while (*p && *p != '\n') p++; continue; }
        p++; // pula o '$'
        char name[ENV_NAME_LEN+1];
        int ni = 0;
        while (*p && *p != '=' && *p != '\n' && ni < ENV_NAME_LEN) {
            name[ni++] = *p++;
        }
        name[ni] = '\0';
        if (*p != '=') { while (*p && *p != '\n') p++; continue; }
        p++; // pula '='
        char val[ENV_VAL_LEN+1];
        int vi = 0;
        while (*p && *p != '\n' && *p != '\r' && vi < ENV_VAL_LEN) {
            val[vi++] = *p++;
        }
        val[vi] = '\0';
        env_set(name, val);
        while (*p && *p != '\n') p++;
    }
    fsd_kfree_read(data);
}
