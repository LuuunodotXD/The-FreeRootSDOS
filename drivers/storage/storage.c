// drivers/storage/storage.c
#include "storage.h"
static StorageDriver *active = 0;
static StorageDriver *drivers[4]; static int ndrv = 0;

void storage_register(StorageDriver *d) { if (ndrv<4) drivers[ndrv++]=d; }
void storage_init(void) {
    for (int i=0;i<ndrv;i++) if (!drivers[i]->init||drivers[i]->init()==0)
        { active=drivers[i]; return; }
}
int storage_read(uint32_t lba, uint16_t n, uint8_t *b) {
    return (active&&active->read) ? active->read(lba,n,b) : -1;
}
int storage_write(uint32_t lba, uint16_t n, const uint8_t *b) {
    return (active&&active->write) ? active->write(lba,n,b) : -1;
}
