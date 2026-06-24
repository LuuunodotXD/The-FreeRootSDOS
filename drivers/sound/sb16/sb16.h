#ifndef SB16_H
#define SB16_H
#include <stdint.h>

// Detecta e inicializa o SB16. Retorna 0 se encontrado, -1 se ausente.
// Testa bases 0x220, 0x240, 0x260, 0x280 via reset do DSP (handshake 0xAA)
// e confirma versão do DSP >= 4 (exclusivo do SB16).
int  sb16_init(void);

// Retorna 1 se sb16_init() foi bem-sucedido.
int  sb16_present(void);

// Toca nota no canal ch (0-8), frequência em Hz, volume 0-63.
// instr: 0=piano 1=flauta 2=sino 3=baixo 4=click
void sb16_note_on (int ch, uint32_t hz, int vol, int instr);
void sb16_note_off(int ch);

// Atalho: toca no canal 0 com instrumento piano
void sb16_beep(uint32_t hz, int vol);

// Efeitos sonoros para a GUI (mesma interface da AdLib)
void sb16_sfx_open (void);
void sb16_sfx_close(void);
void sb16_sfx_error(void);

// Registra o driver no HAL de som.
// DEVE ser chamado antes de adlib_register() em kernel_main().
void sb16_register(void);

#endif
