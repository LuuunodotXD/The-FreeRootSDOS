#ifndef ADLIB_H
#define ADLIB_H
#include <stdint.h>

// Instrumentos predefinidos
typedef enum {
    ADLIB_PIANO = 0,
    ADLIB_FLUTE,
    ADLIB_BELL,
    ADLIB_BASS,
    ADLIB_CLICK,
    ADLIB_INSTR_COUNT
} AdlibInstr;

// era: void adlib_init(void);
int  adlib_init(void);
int  adlib_present(void);  // ← adicionado

// Toca nota no canal ch (0–8), frequência em Hz, volume 0–63
void adlib_note_on (int ch, uint32_t hz, int vol, int instr);

void adlib_note_off(int ch);

// Atalho: toca no canal 0 com instrumento piano
void adlib_beep(uint32_t hz, int vol);

// Efeitos sonoros para a GUI
void adlib_sfx_open (void);   // janela abrindo
void adlib_sfx_close(void);   // janela fechando
void adlib_sfx_error(void);   // erro

// Registrador do HAL
void adlib_register(void);

#endif
