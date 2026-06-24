#ifndef ENV_H
#define ENV_H

#include <stdint.h>

#define ENV_MAX_VARS  32
#define ENV_NAME_LEN  16
#define ENV_VAL_LEN   64

// Carrega A:.VAR/env.var do disco. Se nao existir, nao faz nada.
void env_init(void);

// Retorna valor da variavel ou "" se nao existe
const char *env_get(const char *name);

// Define ou atualiza variavel
void env_set(const char *name, const char *value);

// Expande $VAR em src para dst (max dst_len bytes)
// $VAR sem espaco seguinte, ou ${VAR}
void env_expand(const char *src, char *dst, int dst_len);
void env_list(void);

#endif
