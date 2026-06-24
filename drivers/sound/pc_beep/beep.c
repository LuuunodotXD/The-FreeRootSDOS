// beep.c — buzzer via porta 0x61 + PIT canal 2
#include "beep.h"
#include "sound.h"
#include "io.h"

static void beep_tone(uint32_t hz) {
    if (!hz) { outb(0x61, inb(0x61) & ~0x03); return; }
    uint32_t div = 1193180 / hz;
    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)(div & 0xFF));
    outb(0x42, (uint8_t)(div >> 8));
    outb(0x61, inb(0x61) | 0x03);
}

static int  b_init(void)    { return 0; }
static int  b_present(void) { return 1; }
static void b_beep(uint32_t hz, int v) { (void)v; beep_tone(hz); }
static void b_off(int ch)   { (void)ch; beep_tone(0); }
static void b_open(void)    { beep_tone(880); }
static void b_close(void)   { beep_tone(440); }
static void b_error(void)   { beep_tone(220); }

static SoundDriver pc_beep_drv = {
    "PC Speaker", b_init, b_present,
    0, b_off, b_beep, b_open, b_close, b_error
};
void pc_beep_register(void) { sound_register(&pc_beep_drv); }
