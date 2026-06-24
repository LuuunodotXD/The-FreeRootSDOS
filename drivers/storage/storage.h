// drivers/storage/storage.h
#ifndef STORAGE_H
#define STORAGE_H
#include <stdint.h>

typedef struct {
    const char *name;
    int (*init)(void);
    int (*read) (uint32_t lba, uint16_t count, uint8_t *buf);
    int (*write)(uint32_t lba, uint16_t count, const uint8_t *buf);
} StorageDriver;

void storage_register(StorageDriver *drv);
void storage_init(void);
int  storage_read (uint32_t lba, uint16_t count, uint8_t *buf);
int  storage_write(uint32_t lba, uint16_t count, const uint8_t *buf);
#endif
