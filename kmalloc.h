#ifndef KMALLOC_H
#define KMALLOC_H

#include <stdint.h>

// Inicializa o heap -- chame uma vez em kernel_main
void  kmalloc_init(void);

// Aloca 'size' bytes; retorna 0 se nao ha espaco
void *kmalloc(uint32_t size);

// Libera um bloco alocado por kmalloc
void  kfree(void *ptr);

// Debug: retorna bytes livres no heap
uint32_t kmalloc_free(void);

#endif
