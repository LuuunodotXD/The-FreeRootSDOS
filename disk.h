#ifndef DISK_H
#define DISK_H

#include <stdint.h>

// Lê um setor (512 bytes) do disco via ATA PIO
// lba = endereço lógico do setor (0-based)
// buf deve ter pelo menos 512 bytes
// Retorna 0 ok, -1 erro
int disk_read(uint32_t lba, uint8_t *buf);

// Escreve um setor (512 bytes) no disco via ATA PIO
// Retorna 0 ok, -1 erro
int disk_write(uint32_t lba, const uint8_t *buf);

#endif
