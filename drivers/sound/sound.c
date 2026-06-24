#include "sound.h"

#define MAX_DRV 8
static SoundDriver *drivers[MAX_DRV];
static int          ndrv   = 0;
static SoundDriver *active = 0;

void sound_register(SoundDriver *d) {
    if (ndrv < MAX_DRV) drivers[ndrv++] = d;
}

void sound_init(void) {
    for (int i = 0; i < ndrv; i++) {
        if (drivers[i]->init && drivers[i]->init() == 0) {
            active = drivers[i]; return;
        }
    }
}

int         sound_present(void)     { return active != 0; }
const char *sound_driver_name(void) { return active ? active->name : "nenhum"; }

void sound_note_on(int ch, uint32_t hz, int vol, int instr) {
    if (active && active->note_on) active->note_on(ch, hz, vol, instr);
}
void sound_note_off(int ch) {
    if (active && active->note_off) active->note_off(ch);
}
void sound_beep(uint32_t hz, int vol) {
    if (active && active->beep) active->beep(hz, vol);
}
void sound_sfx_open(void)  { if (active && active->sfx_open)  active->sfx_open();  }
void sound_sfx_close(void) { if (active && active->sfx_close) active->sfx_close(); }
void sound_sfx_error(void) { if (active && active->sfx_error) active->sfx_error(); }
