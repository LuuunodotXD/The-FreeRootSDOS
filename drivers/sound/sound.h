#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>

// Instrumentos genéricos (compatíveis com valores AdLib)
#define SOUND_PIANO  0
#define SOUND_FLUTE  1
#define SOUND_BELL   2
#define SOUND_BASS   3
#define SOUND_CLICK  4

typedef struct {
    const char *name;
    int   (*init)(void);
    int   (*present)(void);
    void  (*note_on) (int ch, uint32_t hz, int vol, int instr);
    void  (*note_off)(int ch);
    void  (*beep)    (uint32_t hz, int vol);
    void  (*sfx_open)(void);
    void  (*sfx_close)(void);
    void  (*sfx_error)(void);
} SoundDriver;

void        sound_register(SoundDriver *drv);
void        sound_init(void);
int         sound_present(void);
const char *sound_driver_name(void);

void sound_note_on (int ch, uint32_t hz, int vol, int instr);
void sound_note_off(int ch);
void sound_beep    (uint32_t hz, int vol);
void sound_sfx_open(void);
void sound_sfx_close(void);
void sound_sfx_error(void);
#endif
