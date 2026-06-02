// keyboard.c
#include <stdint.h>
#include "keyboard.h"
#include "io.h"

// ----------------------------------------------------------------
// Ring buffer
// ----------------------------------------------------------------

#define KBUF_SIZE 64

static volatile int kbuf[KBUF_SIZE];   // int para armazenar KEY_* > 127
static volatile int kbuf_head = 0;
static volatile int kbuf_tail = 0;

static void kbuf_put(int val) {
    int next = (kbuf_head + 1) % KBUF_SIZE;
    if (next != kbuf_tail) {
        kbuf[kbuf_head] = val;
        kbuf_head = next;
    }
}

// ----------------------------------------------------------------
// Tabelas de scancode — layout US QWERTY completo
// ----------------------------------------------------------------

// Tecla normal (sem shift)
static const char sc_normal[58] = {
/*00*/  0,
/*01*/  27,           // Esc
/*02*/  '1','2','3','4','5','6','7','8','9','0',
/*0C*/  '-','=',
/*0E*/  '\b',         // Backspace
/*0F*/  '\t',         // Tab
/*10*/  'q','w','e','r','t','y','u','i','o','p',
/*1A*/  '[',']',
/*1C*/  '\n',         // Enter
/*1D*/  0,            // Ctrl esquerdo
/*1E*/  'a','s','d','f','g','h','j','k','l',
/*27*/  ';',
/*28*/  '\'',
/*29*/  '`',
/*2A*/  0,            // Shift esquerdo
/*2B*/  '\\',
/*2C*/  'z','x','c','v','b','n','m',
/*33*/  ',','.','/',
/*36*/  0,            // Shift direito
/*37*/  '*',          // Keypad *
/*38*/  0,            // Alt esquerdo
/*39*/  ' '           // Space
};

// Tecla com Shift
static const char sc_shifted[58] = {
/*00*/  0,
/*01*/  27,
/*02*/  '!','@','#','$','%','^','&','*','(',')',
/*0C*/  '_','+',
/*0E*/  '\b',
/*0F*/  '\t',
/*10*/  'Q','W','E','R','T','Y','U','I','O','P',
/*1A*/  '{','}',
/*1C*/  '\n',
/*1D*/  0,
/*1E*/  'A','S','D','F','G','H','J','K','L',
/*27*/  ':',
/*28*/  '"',
/*29*/  '~',
/*2A*/  0,
/*2B*/  '|',
/*2C*/  'Z','X','C','V','B','N','M',
/*33*/  '<','>','?',
/*36*/  0,
/*37*/  '*',
/*38*/  0,
/*39*/  ' '
};

// ----------------------------------------------------------------
// Estado do teclado
// ----------------------------------------------------------------

static volatile int shift_held    = 0;
static volatile int caps_lock     = 0;
static volatile int ctrl_held     = 0;  // 1 se Ctrl pressionado
static volatile int alt_held      = 0;  // 1 se Alt pressionado
static volatile int ext_pending   = 0;

// ----------------------------------------------------------------
// IRQ1 handler — chamado pelo stub em idt.c
// ----------------------------------------------------------------

void keyboard_irq(void) {
    uint8_t sc = inb(0x60);

    // Prefixo de tecla estendida (setas, Home, End, Del, etc.)
    if (sc == 0xE0) {
        ext_pending = 1;
        outb(0x20, 0x20);
        return;
    }

    // Key release (bit 7 set)
    if (sc & 0x80) {
        uint8_t rel = sc & 0x7F;
        if (rel == 0x2A || rel == 0x36) shift_held = 0;
        if (rel == 0x1D) ctrl_held = 0;
        if (rel == 0x38) alt_held  = 0;
        if (ext_pending) ext_pending = 0;
        outb(0x20, 0x20);
        return;
    }

    // Tecla estendida (segundo byte após 0xE0)
    if (ext_pending) {
        ext_pending = 0;
        int key = 0;
        switch (sc) {
            case 0x4B: key = KEY_LEFT;  break;
            case 0x4D: key = KEY_RIGHT; break;
            case 0x47: key = KEY_HOME;  break;
            case 0x4F: key = KEY_END;   break;
            case 0x53: key = KEY_DEL;   break;
            case 0x48: key = KEY_UP;    break;
            case 0x50: key = KEY_DOWN;  break;
        }
        if (key) kbuf_put(key);
        outb(0x20, 0x20);
        return;
    }


    // Alt press (esquerdo = 0x38)
    if (sc == 0x38) { alt_held = 1; outb(0x20, 0x20); return; }

    // Ctrl press (esquerdo = 0x1D)
    if (sc == 0x1D) { ctrl_held = 1; outb(0x20, 0x20); return; }

    // Shift press
    if (sc == 0x2A || sc == 0x36) { shift_held = 1; outb(0x20, 0x20); return; }

    // Caps Lock toggle
    if (sc == 0x3A) { caps_lock ^= 1; outb(0x20, 0x20); return; }

    // Alt+Fn e Ctrl+Alt+Fn -> terminais (F1-F8 = scancodes 0x3B-0x42)
    if (alt_held && sc >= 0x3B && sc <= 0x42) {
        int fn = sc - 0x3B;
        kbuf_put(ctrl_held ? (0xB0 + fn) : (0xA0 + fn));
        outb(0x20, 0x20);
        return;
    }

    // Ctrl+letra: retorna codigo de controle ASCII (1-26)
    if (ctrl_held && sc < sizeof(sc_normal)) {
        char c = sc_normal[sc];
        if (c >= 'a' && c <= 'z') { kbuf_put(c - 'a' + 1); outb(0x20, 0x20); return; }
        if (c >= 'A' && c <= 'Z') { kbuf_put(c - 'A' + 1); outb(0x20, 0x20); return; }
    }

    // Alt+F1..F8 → troca de terminal
    if (alt_held && sc >= 0x3B && sc <= 0x42) {
        kbuf_put(KEY_ALT_F1 + (sc - 0x3B));
        outb(0x20, 0x20); return;
    }

    // Tecla normal: decide normal vs shifted
    if (sc < sizeof(sc_normal)) {
        char c = sc_normal[sc];
        if (c) {
            int use_shift = shift_held;
            // Caps lock inverte shift para letras
            if (c >= 'a' && c <= 'z') use_shift ^= caps_lock;
            if (use_shift && sc < sizeof(sc_shifted) && sc_shifted[sc])
                c = sc_shifted[sc];
            kbuf_put((int)(unsigned char)c);
        }
    }

    outb(0x20, 0x20);
}

// ----------------------------------------------------------------
// ----------------------------------------------------------------
// keyboard_available -- nao bloqueia, retorna 1 se ha tecla
// ----------------------------------------------------------------

int keyboard_available(void) {
    return kbuf_head != kbuf_tail;
}

// getchar — bloqueia até ter dado no buffer
// ----------------------------------------------------------------

int getchar(void) {
    while (kbuf_head == kbuf_tail)
        asm volatile ("hlt");
    int val = kbuf[kbuf_tail];
    kbuf_tail = (kbuf_tail + 1) % KBUF_SIZE;
    return val;
}

// getchar_nonblock — retorna o próximo caractere sem bloquear (-1 se vazio)
// ----------------------------------------------------------------

int getchar_nonblock(void) {
    if (kbuf_head == kbuf_tail) return -1;
    int val = kbuf[kbuf_tail];
    kbuf_tail = (kbuf_tail + 1) % KBUF_SIZE;
    return val;
}
