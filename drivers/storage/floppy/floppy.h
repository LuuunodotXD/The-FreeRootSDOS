#ifndef FLOPPY_H
#define FLOPPY_H
#include <stdint.h>

int floppy_init(void);
int floppy_read (uint32_t lba, uint16_t count, uint8_t *buf);
int floppy_write(uint32_t lba, uint16_t count, const uint8_t *buf);
void floppy_register(void);   // registra no storage HAL

#endif
