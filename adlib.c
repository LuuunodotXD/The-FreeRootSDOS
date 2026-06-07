#include "adlib.h"
#include "io.h"
#include "idt.h"   // timer_sleep

#define OPL2_ADDR  0x388
#define OPL2_DATA  0x389

// Offsets dos operadores por canal
static const uint8_t OP_MOD[9] = {  0,  1,  2,  6,  7,  8, 12, 13, 14 };
static const uint8_t OP_CAR[9] = {  3,  4,  5,  9, 10, 11, 15, 16, 17 };

// Último valor de B0 por canal (para note_off preservar pitch)
static uint8_t ch_b0[9];

// ----------------------------------------------------------------
// Escrita com timing correto (OPL2 precisa de delays via reads)
// ----------------------------------------------------------------
static void opl2_write(uint8_t reg, uint8_t val) {
    outb(OPL2_ADDR, reg);
    inb(OPL2_ADDR); inb(OPL2_ADDR); inb(OPL2_ADDR);
    inb(OPL2_ADDR); inb(OPL2_ADDR); inb(OPL2_ADDR);
    outb(OPL2_DATA, val);
    for (int i = 0; i < 35; i++) inb(OPL2_ADDR);
}

// ----------------------------------------------------------------
// Instrumentos
// ----------------------------------------------------------------
typedef struct {
    uint8_t m_mult, m_level, m_adsr1, m_adsr2, m_wave;
    uint8_t c_mult, c_level, c_adsr1, c_adsr2, c_wave;
    uint8_t feedback;
} AdlibPatch;

static const AdlibPatch patches[ADLIB_INSTR_COUNT] = {
    // PIANO: ataque rápido, decay médio, sustain baixo
    { 0x01, 0x00, 0xF3, 0x75, 0x00,
      0x01, 0x00, 0xF3, 0x75, 0x00, 0x0E },

    // FLUTE: ataque suave, sustain longo, onda senoidal
    { 0x02, 0x00, 0xA5, 0x33, 0x00,
      0x02, 0x00, 0xA5, 0x33, 0x00, 0x06 },

    // BELL: ataque instantâneo, decay rápido, sem sustain
    { 0x05, 0x00, 0xF0, 0x97, 0x01,
      0x02, 0x00, 0xF0, 0xA7, 0x00, 0x0A },

    // BASS: frequência baixa, ataque rápido
    { 0x00, 0x00, 0xF8, 0x65, 0x00,
      0x00, 0x00, 0xF6, 0x65, 0x00, 0x08 },

    // CLICK: EGT=1 (bit 5) faz o envelope decair até zero sozinho
    // 0x21 = AM=0 VIB=0 EGT=1 KSR=0 MULT=1
    { 0x21, 0x00, 0xFF, 0x08, 0x00,
      0x21, 0x00, 0xFF, 0x08, 0x00, 0x0E },
};

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
    opl2_write(0x40 + c, atten & 0x3F);   // volume no portador
    opl2_write(0x60 + c, p->c_adsr1);
    opl2_write(0x80 + c, p->c_adsr2);
    opl2_write(0xE0 + c, p->c_wave);

    opl2_write(0xC0 + ch, p->feedback);
}

// ----------------------------------------------------------------
// Frequência → block + fnum
// ----------------------------------------------------------------
static void hz_to_fnum(uint32_t hz, int *block, uint32_t *fnum) {
    *block = 0;
    uint32_t fn = (hz << 20) / 49716;
    while (fn > 1023 && *block < 7) { fn >>= 1; (*block)++; }
    if (fn > 1023) fn = 1023;
    *fnum = fn;
}

// ----------------------------------------------------------------
// API pública
// ----------------------------------------------------------------
void adlib_init(void) {
    opl2_write(0x01, 0x20);  // habilita waveform select
    opl2_write(0xBD, 0x00);  // percussão desligada

    for (int i = 0; i < 9; i++) {
        ch_b0[i] = 0;
        opl2_write(0xB0 + i, 0x00);
        opl2_write(0x40 + OP_MOD[i], 0x3F);
        opl2_write(0x40 + OP_CAR[i], 0x3F);
    }
}

void adlib_note_on(int ch, uint32_t hz, int vol, AdlibInstr instr) {
    if (ch < 0 || ch > 8) return;
    if ((unsigned)instr >= ADLIB_INSTR_COUNT) instr = ADLIB_PIANO;

    // Limpa key-on para permitir re-trigger
    opl2_write(0xB0 + ch, ch_b0[ch] & (uint8_t)~0x20);

    apply_patch(ch, &patches[instr], vol);

    int block; uint32_t fnum;
    hz_to_fnum(hz, &block, &fnum);

    opl2_write(0xA0 + ch, fnum & 0xFF);
    uint8_t b0 = (uint8_t)(0x20 | ((block & 0x07) << 2) | ((fnum >> 8) & 0x03));
    ch_b0[ch]  = b0;
    opl2_write(0xB0 + ch, b0);
}

void adlib_note_off(int ch) {
    if (ch < 0 || ch > 8) return;
    opl2_write(0xB0 + ch, ch_b0[ch] & (uint8_t)~0x20);
}

void adlib_beep(uint32_t hz, int vol) {
    adlib_note_on(0, hz, vol, ADLIB_PIANO);
}

// ----------------------------------------------------------------
// Efeitos sonoros para a GUI
// ----------------------------------------------------------------
void adlib_sfx_open(void) {
    adlib_note_on(8, 784, 45, ADLIB_PIANO);
    timer_sleep(100);
    adlib_note_off(8);
}

void adlib_sfx_close(void) {
    adlib_note_on(8, 523, 45, ADLIB_PIANO);
    timer_sleep(100);
    adlib_note_off(8);
}

void adlib_sfx_error(void) {
    adlib_note_on(8, 220, 45, ADLIB_PIANO);
    timer_sleep(100);
    adlib_note_off(8);
}
