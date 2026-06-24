#include "floppy.h"
#include "storage.h"
#include "idt.h"   // timer_get_ticks
#include "io.h"
#include <stdint.h>

// ---- Portas do FDC (controlador primário, drive 0 = A:) ----
#define FDC_DOR    0x3F2   // Digital Output Register
#define FDC_MSR    0x3F4   // Main Status Register
#define FDC_DATA   0x3F5   // Data FIFO

// ---- Geometria padrão: floppy 3.5" 1.44MB ----
#define FD_HEADS        2
#define FD_SECTORS      18      // por trilha
#define FD_SECTOR_SIZE  512

static int ready = 0;

// ---- Buffer de DMA: precisa ficar dentro de uma única página de 64KB ----
static uint8_t dma_raw[1024] __attribute__((aligned(512)));
static uint8_t *dma_buf;

static void dma_buf_init(void) {
    uint32_t addr = (uint32_t)dma_raw;          // já alinhado a 512
    if ((addr & 0xFFFF) > (0x10000 - FD_SECTOR_SIZE))
        addr += FD_SECTOR_SIZE;                  // evita cruzar limite de 64KB
    dma_buf = (uint8_t *)addr;
}

// ---- Espera MSR ficar pronto pra escrita (RQM=1, DIO=0) ----
static int fdc_wait_write(void) {
    int timeout = 100000;
    while (timeout--) {
        uint8_t msr = inb(FDC_MSR);
        if ((msr & 0xC0) == 0x80) return 1;   // RQM=1, DIO=0
    }
    return 0;
}

// ---- Espera MSR ficar pronto pra leitura (RQM=1, DIO=1) ----
static int fdc_wait_read(void) {
    int timeout = 100000;
    while (timeout--) {
        uint8_t msr = inb(FDC_MSR);
        if ((msr & 0xC0) == 0xC0) return 1;   // RQM=1, DIO=1
    }
    return 0;
}

static int fdc_send(uint8_t byte) {
    if (!fdc_wait_write()) return 0;
    outb(FDC_DATA, byte);
    return 1;
}
static int fdc_recv(uint8_t *byte) {
    if (!fdc_wait_read()) return 0;
    *byte = inb(FDC_DATA);
    return 1;
}

// ---- SENSE INTERRUPT STATUS — obrigatório após cada IRQ do FDC ----
static void fdc_sense_interrupt(uint8_t *st0, uint8_t *cyl) {
    fdc_send(0x08);
    fdc_recv(st0);
    fdc_recv(cyl);
}

static void delay_ms(uint32_t ms) {
    uint32_t t0 = timer_get_ticks();
    while (timer_get_ticks() - t0 < ms) { }
}

// ---- Liga o motor do drive A e aguarda estabilizar ----
static void motor_on(void) {
    outb(FDC_DOR, 0x1C);   // drive0 | ~RESET | DMA/IRQ | motorA
    delay_ms(300);          // tempo de spin-up (irrelevante no QEMU, importa em hw real)
}
static void motor_off(void) {
    outb(FDC_DOR, 0x0C);   // drive0 | ~RESET | DMA/IRQ, motor desligado
}

// ---- RECALIBRATE: leva o cabeçote pra trilha 0 ----
static int fdc_recalibrate(void) {
    fdc_send(0x07);
    fdc_send(0x00);        // drive 0
    delay_ms(15);
    uint8_t st0, cyl;
    fdc_sense_interrupt(&st0, &cyl);
    return (st0 & 0xC0) == 0;   // bits7:6 = 0 → terminação normal
}

// ---- Programa o canal 2 de DMA (8237) ----
static void dma_setup(int write_to_fdc) {
    uint32_t addr = (uint32_t)dma_buf;
    uint16_t count = FD_SECTOR_SIZE - 1;

    outb(0x0A, 0x06);                  // mascara canal 2
    outb(0x0C, 0x00);                  // reseta flip-flop

    uint8_t mode = write_to_fdc ? 0x4A : 0x46;  // 0x46=leitura(FDC→RAM) 0x4A=escrita(RAM→FDC)
    outb(0x0B, mode);

    outb(0x04, (uint8_t)(addr & 0xFF));
    outb(0x04, (uint8_t)((addr >> 8) & 0xFF));
    outb(0x81, (uint8_t)((addr >> 16) & 0xFF));  // página (bits 23:16)

    outb(0x05, (uint8_t)(count & 0xFF));
    outb(0x05, (uint8_t)((count >> 8) & 0xFF));

    outb(0x0A, 0x02);                  // desmascara canal 2
}

static void lba_to_chs(uint32_t lba, uint8_t *cyl, uint8_t *head, uint8_t *sector) {
    *cyl    = (uint8_t)(lba / (FD_SECTORS * FD_HEADS));
    *head   = (uint8_t)((lba / FD_SECTORS) % FD_HEADS);
    *sector = (uint8_t)((lba % FD_SECTORS) + 1);
}

// ---- Transferência de 1 setor (leitura ou escrita) ----
static int fdc_rw_sector(uint32_t lba, uint8_t *buf, int do_write) {
    uint8_t cyl, head, sector;
    lba_to_chs(lba, &cyl, &head, &sector);

    if (do_write)
        for (int i = 0; i < FD_SECTOR_SIZE; i++) dma_buf[i] = buf[i];

    dma_setup(do_write);

    uint8_t cmd = do_write ? 0xC5 : 0xE6;   // WRITE(MT|MFM) / READ(MT|MFM|SK)
    fdc_send(cmd);
    fdc_send((uint8_t)(head << 2));         // drive=0, head
    fdc_send(cyl);
    fdc_send(head);
    fdc_send(sector);
    fdc_send(2);                            // 512 bytes/setor
    fdc_send(FD_SECTORS);                   // último setor da trilha
    fdc_send(0x1B);                         // GAP3 padrão (1.44MB)
    fdc_send(0xFF);                         // DTL (ignorado quando code!=0)

    // Lê os 7 bytes de resultado
    uint8_t st0, st1, st2, rcyl, rhead, rsec, rsz;
    fdc_recv(&st0); fdc_recv(&st1); fdc_recv(&st2);
    fdc_recv(&rcyl); fdc_recv(&rhead); fdc_recv(&rsec); fdc_recv(&rsz);
    (void)rcyl; (void)rhead; (void)rsec; (void)rsz;

    if ((st0 & 0xC0) != 0) return -1;       // erro de terminação
    if (st1 != 0 || st2 != 0)  return -1;

    if (!do_write)
        for (int i = 0; i < FD_SECTOR_SIZE; i++) buf[i] = dma_buf[i];

    return 0;
}

static int fdc_rw_sector_retry(uint32_t lba, uint8_t *buf, int do_write) {
    for (int attempt = 0; attempt < 3; attempt++) {
        if (fdc_rw_sector(lba, buf, do_write) == 0) return 0;
        fdc_recalibrate();   // re-sincroniza antes de tentar de novo
    }
    return -1;
}

// ---- API pública ----
int floppy_init(void) {
    dma_buf_init();

    outb(FDC_DOR, 0x00);   // reset assertado
    delay_ms(10);
    outb(FDC_DOR, 0x0C);   // reset liberado, controlador habilitado

    // Após reset, o FDC gera uma interrupção "fantasma" por unidade — precisa limpar
    for (int i = 0; i < 4; i++) {
        uint8_t st0, cyl;
        fdc_sense_interrupt(&st0, &cyl);
    }

    // SPECIFY: define timings (valores padrão amplamente usados, seguros p/ 1.44MB)
    fdc_send(0x03);
    fdc_send(0xDF);
    fdc_send(0x02);          // bit0=0 → modo DMA habilitado (não-PIO)

    motor_on();
    int ok = fdc_recalibrate();
    motor_off();

    ready = ok;
    return ok ? 0 : -1;
}

int floppy_read(uint32_t lba, uint16_t count, uint8_t *buf) {
    if (!ready) return -1;
    motor_on();
    int rc = 0;
    for (uint16_t i = 0; i < count; i++) {
        if (fdc_rw_sector_retry(lba + i, buf + (uint32_t)i * FD_SECTOR_SIZE, 0) != 0) {
            rc = -1; break;
        }
    }
    motor_off();
    return rc;
}

int floppy_write(uint32_t lba, uint16_t count, const uint8_t *buf) {
    if (!ready) return -1;
    motor_on();
    int rc = 0;
    for (uint16_t i = 0; i < count; i++) {
        if (fdc_rw_sector_retry(lba + i, (uint8_t *)buf + (uint32_t)i * FD_SECTOR_SIZE, 1) != 0) {
            rc = -1; break;
        }
    }
    motor_off();
    return rc;
}

// ---- Registro no HAL de storage ----
static StorageDriver floppy_drv = {
    "Floppy (FDC)", floppy_init, floppy_read, floppy_write
};
void floppy_register(void) { storage_register(&floppy_drv); }
