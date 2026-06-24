// sb16.c — Driver Sound Blaster 16
//
// Detecção: reset do DSP (handshake 0xAA) + versão >= 4.xx
// Som:      OPL3 do próprio SB16 (portas base+0x0/0x1, banco baixo,
//           compatível OPL2) — sem conflito com AdLib em 0x388
// Beeps/SFX: via OPL3 (canal 8), como a AdLib
//
// Registro: sb16_register() deve ser chamado ANTES de adlib_register().
// O sound_init() para no primeiro init() bem-sucedido, então se o SB16
// for encontrado a AdLib nunca é inicializada — sem 2 drivers ativos.

#include "sb16.h"
#include "sound.h"
#include "io.h"
#include "idt.h"   // timer_sleep
#include <stdint.h>

// ----------------------------------------------------------------
// Offsets de porta (relativos à base I/O)
// ----------------------------------------------------------------
#define DSP_RESET       0x06   // W: escreve 1/0 para resetar
#define DSP_READ        0x0A   // R: lê byte do DSP
#define DSP_WRITE       0x0C   // W: escreve comando/dado ao DSP
#define DSP_READ_STAT   0x0E   // R: bit7=1 → dado disponível em DSP_READ

#define OPL3_ADDR_LO    0x00   // OPL3 banco baixo — reg select
#define OPL3_DATA_LO    0x01   // OPL3 banco baixo — dado
// (OPL3 banco alto em +0x02/+0x03, não usado aqui — OPL2 compat)

// Bases I/O candidatas (jumpers padrão / PnP mais comuns)
static const uint16_t SB_BASES[] = { 0x220, 0x240, 0x260, 0x280, 0x000 };

static uint16_t sb_base  = 0;   // 0 = não detectado
static int      sb_ready = 0;

// ----------------------------------------------------------------
// DSP helpers
// ----------------------------------------------------------------

// Aguarda DSP pronto para receber comando (~timeout generoso)
static int dsp_wait_write(void) {
    for (int t = 0; t < 65535; t++)
        if (!(inb(sb_base + DSP_WRITE) & 0x80)) return 0;
    return -1;   // timeout
}

// Aguarda dado disponível para leitura
static int dsp_wait_read(void) {
    for (int t = 0; t < 65535; t++)
        if (inb(sb_base + DSP_READ_STAT) & 0x80) return 0;
    return -1;   // timeout
}

static int dsp_write_cmd(uint8_t cmd) {
    if (dsp_wait_write() != 0) return -1;
    outb(sb_base + DSP_WRITE, cmd);
    return 0;
}

static int dsp_read_byte(uint8_t *out) {
    if (dsp_wait_read() != 0) return -1;
    *out = inb(sb_base + DSP_READ);
    return 0;
}

// Reset do DSP numa base específica — retorna 0 se responder 0xAA
static int dsp_reset(uint16_t base) {
    outb(base + DSP_RESET, 1);
    // Delay mínimo de ~3 µs via I/O reads
    for (int i = 0; i < 300; i++) inb(base + DSP_READ_STAT);
    outb(base + DSP_RESET, 0);

    // Aguarda resposta 0xAA em até ~100 ms
    for (int t = 0; t < 100000; t++) {
        if ((inb(base + DSP_READ_STAT) & 0x80) &&
            inb(base + DSP_READ) == 0xAA)
            return 0;
    }
    return -1;
}

// ----------------------------------------------------------------
// OPL3 — banco baixo (compatível OPL2, 9 canais)
// ----------------------------------------------------------------
static const uint8_t OP_MOD[9] = { 0,  1,  2,  6,  7,  8, 12, 13, 14 };
static const uint8_t OP_CAR[9] = { 3,  4,  5,  9, 10, 11, 15, 16, 17 };
static uint8_t ch_b0[9];

static void opl_write(uint8_t reg, uint8_t val) {
    outb(sb_base + OPL3_ADDR_LO, reg);
    for (int i = 0; i < 6;  i++) inb(sb_base + OPL3_ADDR_LO);  // ~3.3 µs
    outb(sb_base + OPL3_DATA_LO, val);
    for (int i = 0; i < 35; i++) inb(sb_base + OPL3_ADDR_LO);  // ~23 µs
}

// Mesma tabela de patches da AdLib — compatibilidade de timbre
typedef struct {
    uint8_t m_mult, m_level, m_adsr1, m_adsr2, m_wave;
    uint8_t c_mult, c_level, c_adsr1, c_adsr2, c_wave;
    uint8_t feedback;
} OplPatch;

static const OplPatch patches[5] = {
    /* PIANO */ { 0x01,0x00,0xF3,0x75,0x00, 0x01,0x00,0xF3,0x75,0x00, 0x0E },
    /* FLUTE */ { 0x02,0x00,0xA5,0x33,0x00, 0x02,0x00,0xA5,0x33,0x00, 0x06 },
    /* BELL  */ { 0x05,0x00,0xF0,0x97,0x01, 0x02,0x00,0xF0,0xA7,0x00, 0x0A },
    /* BASS  */ { 0x00,0x00,0xF8,0x65,0x00, 0x00,0x00,0xF6,0x65,0x00, 0x08 },
    /* CLICK */ { 0x21,0x00,0xFF,0x08,0x00, 0x21,0x00,0xFF,0x08,0x00, 0x0E },
};

static void apply_patch(int ch, const OplPatch *p, int vol) {
    uint8_t m     = OP_MOD[ch];
    uint8_t c     = OP_CAR[ch];
    uint8_t atten = (uint8_t)(63 - (vol > 63 ? 63 : vol));

    opl_write(0x20 + m, p->m_mult);   opl_write(0x40 + m, p->m_level);
    opl_write(0x60 + m, p->m_adsr1);  opl_write(0x80 + m, p->m_adsr2);
    opl_write(0xE0 + m, p->m_wave);

    opl_write(0x20 + c, p->c_mult);   opl_write(0x40 + c, atten);
    opl_write(0x60 + c, p->c_adsr1);  opl_write(0x80 + c, p->c_adsr2);
    opl_write(0xE0 + c, p->c_wave);

    opl_write(0xC0 + ch, p->feedback);
}

static void hz_to_fnum(uint32_t hz, int *block, uint32_t *fnum) {
    *block    = 0;
    uint32_t fn = (hz << 20) / 49716;
    while (fn > 1023 && *block < 7) { fn >>= 1; (*block)++; }
    if (fn > 1023) fn = 1023;
    *fnum = fn;
}

static void opl_hw_init(void) {
    opl_write(0x01, 0x20);   // habilita waveform select (OPL2 compat)
    opl_write(0xBD, 0x00);   // percussão desligada
    for (int i = 0; i < 9; i++) {
        ch_b0[i] = 0;
        opl_write(0xB0 + i, 0x00);
        opl_write(0x40 + OP_MOD[i], 0x3F);   // silencia modulador
        opl_write(0x40 + OP_CAR[i], 0x3F);   // silencia portador
    }
}

// ----------------------------------------------------------------
// API pública
// ----------------------------------------------------------------

int sb16_init(void) {
    // 1. Procura a placa nas bases candidatas
    for (int i = 0; SB_BASES[i]; i++) {
        if (dsp_reset(SB_BASES[i]) == 0) {
            sb_base = SB_BASES[i];
            break;
        }
    }
    if (!sb_base) return -1;   // nenhuma SB encontrada

    // 2. Confirma versão do DSP >= 4 (SB16 = 4.xx; SBPro = 3.xx)
    if (dsp_write_cmd(0xE1) != 0) { sb_base = 0; return -1; }
    uint8_t major = 0, minor = 0;
    if (dsp_read_byte(&major) != 0 || dsp_read_byte(&minor) != 0) {
        sb_base = 0; return -1;
    }
    (void)minor;
    if (major < 4) { sb_base = 0; return -1; }   // não é SB16

    // 3. Liga o speaker do DSP (habilita saída analógica)
    dsp_write_cmd(0xD1);

    // 4. Inicializa o OPL3 no banco baixo
    opl_hw_init();

    sb_ready = 1;
    return 0;
}

int sb16_present(void) { return sb_ready; }

void sb16_note_on(int ch, uint32_t hz, int vol, int instr) {
    if (!sb_ready || ch < 0 || ch > 8) return;
    if ((unsigned)instr >= 5) instr = 0;
    opl_write(0xB0 + ch, ch_b0[ch] & (uint8_t)~0x20);   // key-off
    apply_patch(ch, &patches[instr], vol);
    int block; uint32_t fnum;
    hz_to_fnum(hz, &block, &fnum);
    opl_write(0xA0 + ch, fnum & 0xFF);
    uint8_t b0 = (uint8_t)(0x20 | ((block & 7) << 2) | ((fnum >> 8) & 3));
    ch_b0[ch]  = b0;
    opl_write(0xB0 + ch, b0);   // key-on
}

void sb16_note_off(int ch) {
    if (!sb_ready || ch < 0 || ch > 8) return;
    opl_write(0xB0 + ch, ch_b0[ch] & (uint8_t)~0x20);
}

void sb16_beep(uint32_t hz, int vol) {
    sb16_note_on(0, hz, vol, 0 /* PIANO */);
}

void sb16_sfx_open(void) {
    if (!sb_ready) return;
    sb16_note_on(8, 784, 45, 0); timer_sleep(100); sb16_note_off(8);
}
void sb16_sfx_close(void) {
    if (!sb_ready) return;
    sb16_note_on(8, 523, 45, 0); timer_sleep(100); sb16_note_off(8);
}
void sb16_sfx_error(void) {
    if (!sb_ready) return;
    sb16_note_on(8, 220, 45, 0); timer_sleep(100); sb16_note_off(8);
}

// ----------------------------------------------------------------
// Registro no HAL de som
// ----------------------------------------------------------------
static SoundDriver sb16_drv = {
    "Sound Blaster 16",
    sb16_init,
    sb16_present,
    sb16_note_on,
    sb16_note_off,
    sb16_beep,
    sb16_sfx_open,
    sb16_sfx_close,
    sb16_sfx_error
};

// Registro incondicional — detecção real ocorre em sb16_init()
// via sound_init(). Deve ser chamado ANTES de adlib_register().
void sb16_register(void) {
    sound_register(&sb16_drv);
}
