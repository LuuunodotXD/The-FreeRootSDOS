// kmalloc.c
// Alocador de heap simples com bitmap de blocos fixos.
//
// Layout:
//   HEAP_START  .... HEAP_END   (64 KB de dados)
//   Bitmap: 1 bit por bloco, empacotado em uint8_t
//
// Bloco = 16 bytes  →  64 KB / 16 = 4096 blocos
// Bitmap = 4096 bits = 512 bytes (alocado estaticamente, fora do heap)
//
// kmalloc(size) encontra N blocos contíguos livres (first-fit),
// marca-os como usados e retorna o ponteiro.
// O cabeçalho de 4 bytes antes do bloco guarda o número de blocos
// alocados para que kfree saiba quantos liberar.
//
// Overhead por alocação: 1 bloco (16 bytes) de cabeçalho.

#include <stdint.h>
#include "kmalloc.h"

#define HEAP_START  0x200000u       // 2 MB — acima do kernel
#define HEAP_SIZE   (64u * 1024u)   // 64 KB
#define BLOCK_SIZE  16u
#define NUM_BLOCKS  (HEAP_SIZE / BLOCK_SIZE)   // 4096

static uint8_t bitmap[NUM_BLOCKS / 8];   // 512 bytes, 1 bit por bloco

// Marca bloco como usado (1) ou livre (0)
static void bitmap_set(uint32_t blk, int used) {
    if (used) bitmap[blk / 8] |=  (1u << (blk % 8));
    else      bitmap[blk / 8] &= ~(1u << (blk % 8));
}

static int bitmap_get(uint32_t blk) {
    return (bitmap[blk / 8] >> (blk % 8)) & 1;
}

void kmalloc_init(void) {
    // Zera o bitmap (todos livres)
    for (uint32_t i = 0; i < sizeof(bitmap); i++) bitmap[i] = 0;
}

void *kmalloc(uint32_t size) {
    if (size == 0) return 0;

    // Blocos necessários: 1 de cabeçalho + ceil(size / BLOCK_SIZE)
    uint32_t data_blocks = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    uint32_t total       = data_blocks + 1;   // +1 para o cabeçalho

    // First-fit: procura 'total' blocos contíguos livres
    uint32_t start = 0, count = 0;
    for (uint32_t i = 0; i < NUM_BLOCKS; i++) {
        if (!bitmap_get(i)) {
            if (count == 0) start = i;
            count++;
            if (count == total) goto found;
        } else {
            count = 0;
        }
    }
    return 0;   // sem espaço

found:
    // Marca todos os blocos como usados
    for (uint32_t i = start; i < start + total; i++) bitmap_set(i, 1);

    // Escreve o cabeçalho no primeiro bloco: número de blocos totais
    uint32_t *header = (uint32_t*)(HEAP_START + start * BLOCK_SIZE);
    *header = total;

    // Retorna ponteiro para o segundo bloco (após o cabeçalho)
    return (void*)(HEAP_START + (start + 1) * BLOCK_SIZE);
}

void kfree(void *ptr) {
    if (!ptr) return;

    // O cabeçalho está um bloco antes do ponteiro retornado
    uint32_t addr  = (uint32_t)ptr;
    uint32_t start = (addr - BLOCK_SIZE - HEAP_START) / BLOCK_SIZE;
    uint32_t total = *(uint32_t*)(HEAP_START + start * BLOCK_SIZE);

    for (uint32_t i = start; i < start + total; i++) bitmap_set(i, 0);
}

uint32_t kmalloc_free(void) {
    uint32_t free_blocks = 0;
    for (uint32_t i = 0; i < NUM_BLOCKS; i++)
        if (!bitmap_get(i)) free_blocks++;
    return free_blocks * BLOCK_SIZE;
}
