#include "adlib.h"
#include "io.h"
#include "idt.h"   // timer_sleep
#include "sound.h"

#define OPL2_ADDR  0x388
#define OPL2_DATA  0x389

static const uint8_t OP_MOD[9] = { 0, 1, 2, 6, 7, 8, 12, 13, 14 };
static const uint8_t OP_CAR[9] = { 3, 4, 5, 9, 10, 11, 15, 16, 17 };
static uint8_t ch_b0[9];
static int adlib_ready = 1;   // flag de presença
static uint8_t opl2_read(uint8_t reg) {
    outb(OPL2_ADDR, reg);
    for (int i = 0; i < 6; i++) inb(OPL2_ADDR);
    return inb(OPL2_DATA);
}

// ----------------------------------------------------------------
// Definição da estrutura de patch (instrumento)
// ----------------------------------------------------------------
typedef struct {
    uint8_t m_mult, m_level, m_adsr1, m_adsr2, m_wave;
    uint8_t c_mult, c_level, c_adsr1, c_adsr2, c_wave;
    uint8_t feedback;
} AdlibPatch;

// ----------------------------------------------------------------
// Tabela de instrumentos
// ----------------------------------------------------------------
static const AdlibPatch patches[ADLIB_INSTR_COUNT] = {
    // PIANO
    { 0x01, 0x00, 0xF3, 0x75, 0x00,
      0x01, 0x00, 0xF3, 0x75, 0x00, 0x0E },
    // FLUTE
    { 0x02, 0x00, 0xA5, 0x33, 0x00,
      0x02, 0x00, 0xA5, 0x33, 0x00, 0x06 },
    // BELL
    { 0x05, 0x00, 0xF0, 0x97, 0x01,
      0x02, 0x00, 0xF0, 0xA7, 0x00, 0x0A },
    // BASS
    { 0x00, 0x00, 0xF8, 0x65, 0x00,
      0x00, 0x00, 0xF6, 0x65, 0x00, 0x08 },
    // CLICK
    { 0x21, 0x00, 0xFF, 0x08, 0x00,
      0x21, 0x00, 0xFF, 0x08, 0x00, 0x0E },
};

// ----------------------------------------------------------------
// Escrita com timing
// ----------------------------------------------------------------
static void opl2_write(uint8_t reg, uint8_t val) {
    outb(OPL2_ADDR, reg);
    for (int i = 0; i < 6; i++) inb(OPL2_ADDR);
    outb(OPL2_DATA, val);
    for (int i = 0; i < 35; i++) inb(OPL2_ADDR);
}

static void apply_patch(int ch, const AdlibPatch *p, int vol) {
    uint8_t m = OP_MOD[ch];
    uint8_t c = OP_CAR[ch];
    uint8_t atten = (uint8_t)(63 - (vol > 63 ? 63 : vol));

    opl2_write(0x20 + m, p->m_mult);
    opl2_write(0x40 + m, p->m_level);
    opl2_write(0x60 + m, p->m_adsr1);
    opl2_write(0x80 + m, p->m_adsr2);
    opl2_write(0xE0 + m, p->m_wave);

    opl2_write(0x20 + c, p->c_mult);
    opl2_write(0x40 + c, atten);   // volume no portador
    opl2_write(0x60 + c, p->c_adsr1);
    opl2_write(0x80 + c, p->c_adsr2);
    opl2_write(0xE0 + c, p->c_wave);

    opl2_write(0xC0 + ch, p->feedback);
}

static void hz_to_fnum(uint32_t hz, int *block, uint32_t *fnum) {
    *block = 0;
    uint32_t fn = (hz << 20) / 49716;
    while (fn > 1023 && *block < 7) {
        fn >>= 1;
        (*block)++;
    }
    if (fn > 1023) fn = 1023;
    *fnum = fn;
}

// ----------------------------------------------------------------
// Inicialização e detecção de presença
// ----------------------------------------------------------------
int adlib_init(void) {
    // ---- Detecção do OPL2 (sequência padrão) ----
    // 1. Reset dos timers
    opl2_write(0x04, 0x60);   // para timer 1 e 2
    opl2_write(0x04, 0x80);   // limpa flag de IRQ

    // 2. Lê status — bits 7,6,5 devem ser 0
    uint8_t status1 = inb(OPL2_ADDR);
    if (status1 & 0xE0) { adlib_ready = 0; return -1; }

    // 3. Liga timer 1
    opl2_write(0x02, 0xFF);   // timer 1 = 255
    opl2_write(0x04, 0x21);   // start timer 1

    // 4. Aguarda (~80μs via reads)
    for (int i = 0; i < 200; i++) inb(OPL2_ADDR);

    // 5. Lê status — bit 7 e bit 6 devem estar setados
    uint8_t status2 = inb(OPL2_ADDR);
    if ((status2 & 0xE0) != 0xC0) { adlib_ready = 0; return -1; }

    // 6. Reset final
    opl2_write(0x04, 0x60);
    opl2_write(0x04, 0x80);

    // ---- Inicialização normal ----
    opl2_write(0x01, 0x20);   // habilita waveform select
    opl2_write(0xBD, 0x00);   // percussão desligada
    for (int i = 0; i < 9; i++) {
        ch_b0[i] = 0;
        opl2_write(0xB0 + i, 0x00);
        opl2_write(0x40 + OP_MOD[i], 0x3F);
        opl2_write(0x40 + OP_CAR[i], 0x3F);
    }
    adlib_ready = 1;
    return 0;
}

int adlib_present(void) {
    return adlib_ready;
}

// ----------------------------------------------------------------
// API de som
// ----------------------------------------------------------------
void adlib_note_on(int ch, uint32_t hz, int vol, int instr) {
    if (!adlib_ready) return;
    if (ch < 0 || ch > 8) return;
    AdlibInstr i = (AdlibInstr)instr;
    if ((unsigned)i >= ADLIB_INSTR_COUNT) i = ADLIB_PIANO;

    // key-off para re-trigger
    opl2_write(0xB0 + ch, ch_b0[ch] & (uint8_t)~0x20);

    apply_patch(ch, &patches[i], vol);

    int block; uint32_t fnum;
    hz_to_fnum(hz, &block, &fnum);

    opl2_write(0xA0 + ch, fnum & 0xFF);
    uint8_t b0 = (uint8_t)(0x20 | ((block & 0x07) << 2) | ((fnum >> 8) & 0x03));
    ch_b0[ch] = b0;
    opl2_write(0xB0 + ch, b0);
}

void adlib_note_off(int ch) {
    if (!adlib_ready) return;
    if (ch < 0 || ch > 8) return;
    opl2_write(0xB0 + ch, ch_b0[ch] & (uint8_t)~0x20);
}

void adlib_beep(uint32_t hz, int vol) {
    adlib_note_on(0, hz, vol, ADLIB_PIANO);
}

void adlib_sfx_open(void) {
    if (!adlib_ready) return;
    adlib_note_on(8, 784, 45, ADLIB_PIANO);
    timer_sleep(100);
    adlib_note_off(8);
}

void adlib_sfx_close(void) {
    if (!adlib_ready) return;
    adlib_note_on(8, 523, 45, ADLIB_PIANO);
    timer_sleep(100);
    adlib_note_off(8);
}

void adlib_sfx_error(void) {
    if (!adlib_ready) return;
    adlib_note_on(8, 220, 45, ADLIB_PIANO);
    timer_sleep(100);
    adlib_note_off(8);
}

// ----------------------------------------------------------------
// Registro no HAL
// ----------------------------------------------------------------
static SoundDriver adlib_drv = {
    "AdLib OPL2",
    adlib_init,
    adlib_present,
    adlib_note_on,
    adlib_note_off,
    adlib_beep,
    adlib_sfx_open,
    adlib_sfx_close,
    adlib_sfx_error
};

void adlib_register(void) {
    if (adlib_present())
        sound_register(&adlib_drv);
}
