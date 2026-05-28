// disk.c -- driver ATA PIO (modo 28-bit LBA)
#include <stdint.h>
#include "disk.h"
#include "io.h"

// Portas do controlador ATA primário
#define ATA_DATA    0x1F0
#define ATA_ERR     0x1F1
#define ATA_COUNT   0x1F2
#define ATA_LBA_LO  0x1F3
#define ATA_LBA_MID 0x1F4
#define ATA_LBA_HI  0x1F5
#define ATA_DRIVE   0x1F6
#define ATA_CMD     0x1F7
#define ATA_STATUS  0x1F7

#define ATA_CMD_READ  0x20
#define ATA_CMD_WRITE 0x30

#define ATA_SR_BSY  0x80   // busy
#define ATA_SR_DRQ  0x08   // data request
#define ATA_SR_ERR  0x01   // error

// Aguarda o drive não estar ocupado
static int ata_wait_ready(void) {
    uint32_t timeout = 0x100000;
    while (timeout--) {
        uint8_t st = inb(ATA_STATUS);
        if (st & ATA_SR_ERR) return -1;
        if (!(st & ATA_SR_BSY) && (st & ATA_SR_DRQ)) return 0;
    }
    return -1;   // timeout
}

// Envia comando LBA e aguarda pronto
static int ata_setup(uint32_t lba, uint8_t cmd) {
    // Aguarda não estar ocupado antes de enviar
    uint32_t t = 0x100000;
    while ((inb(ATA_STATUS) & ATA_SR_BSY) && t--);

    outb(ATA_DRIVE,  0xE0 | ((lba >> 24) & 0x0F)); // LBA mode, drive 0
    outb(ATA_ERR,    0x00);
    outb(ATA_COUNT,  1);
    outb(ATA_LBA_LO, (uint8_t)(lba));
    outb(ATA_LBA_MID,(uint8_t)(lba >> 8));
    outb(ATA_LBA_HI, (uint8_t)(lba >> 16));
    outb(ATA_CMD,    cmd);

    return ata_wait_ready();
}

int disk_read(uint32_t lba, uint8_t *buf) {
    if (ata_setup(lba, ATA_CMD_READ) != 0) return -1;
    for (int i = 0; i < 256; i++) {
        uint16_t w = inw(ATA_DATA);
        buf[i * 2]     = (uint8_t)(w & 0xFF);
        buf[i * 2 + 1] = (uint8_t)(w >> 8);
    }
    return 0;
}

int disk_write(uint32_t lba, const uint8_t *buf) {
    if (ata_setup(lba, ATA_CMD_WRITE) != 0) return -1;
    for (int i = 0; i < 256; i++) {
        uint16_t w = (uint16_t)buf[i * 2] | ((uint16_t)buf[i * 2 + 1] << 8);
        outw(ATA_DATA, w);
    }
    // Flush cache
    outb(ATA_CMD, 0xE7);
    uint32_t t = 0x100000;
    while ((inb(ATA_STATUS) & ATA_SR_BSY) && t--);
    return 0;
}
