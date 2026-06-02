// mouse.c — driver PS/2 para modo gráfico
#include "mouse.h"
#include "io.h"

// ----------------------------------------------------------------
// PS/2 helpers
// ----------------------------------------------------------------

static void ps2_wait_in(void)  { int t=100000; while ((inb(0x64) & 0x02) && t-- > 0); } // aguarda buffer de entrada livre
static void ps2_wait_out(void) { int t=100000; while (!(inb(0x64) & 0x01) && t-- > 0); } // aguarda dado disponível (com timeout)

static void mouse_send(uint8_t byte) {
    ps2_wait_in(); outb(0x64, 0xD4); // próximo byte vai ao mouse
    ps2_wait_in(); outb(0x60, byte);
}

static uint8_t mouse_recv(void) {
    ps2_wait_out();
    return inb(0x60);
}

// ----------------------------------------------------------------
// Estado
// ----------------------------------------------------------------

static volatile int mx = 320, my = 240;
static volatile int mbtn = 0;
static volatile int mdirty = 0;

static volatile uint8_t pkt[3];
static volatile int     pkt_state = 0;

// ----------------------------------------------------------------
// Inicialização
// ----------------------------------------------------------------

void mouse_init(void) {
    // 1. Habilita dispositivo auxiliar
    ps2_wait_in();
    outb(0x64, 0xA8);

    // 2. Habilita IRQ12 no byte de comando do controlador
    ps2_wait_in(); outb(0x64, 0x20);
    ps2_wait_out();
    uint8_t status = inb(0x60) | 0x02;
    ps2_wait_in(); outb(0x64, 0x60);
    ps2_wait_in(); outb(0x60, status);

    // 3. Defaults + enable
    mouse_send(0xF6); mouse_recv(); // set defaults → ACK
    mouse_send(0xF4); mouse_recv(); // enable        → ACK
}

// ----------------------------------------------------------------
// IRQ12 — chamado pelo stub em idt.c
// ----------------------------------------------------------------

void mouse_irq(void) {
    uint8_t data = inb(0x60);

    // Valida primeiro byte: bit 3 deve ser 1
    if (pkt_state == 0 && !(data & 0x08)) {
        outb(0x20, 0x20); outb(0xA0, 0x20);
        return;
    }

    pkt[pkt_state++] = data;

    if (pkt_state == 3) {
        pkt_state = 0;

        int dx =  (int8_t)pkt[1];
        int dy = -(int8_t)pkt[2]; // eixo Y invertido no PS/2

        mx += dx;
        my += dy;

        if (mx < 0)   mx = 0;
        if (mx > 639) mx = 639;
        if (my < 0)   my = 0;
        if (my > 479) my = 479;

        mbtn   = pkt[0] & 0x07;
        mdirty = 1;
    }

    outb(0x20, 0x20); // EOI PIC1
    outb(0xA0, 0x20); // EOI PIC2
}

// ----------------------------------------------------------------
// API pública
// ----------------------------------------------------------------

int mouse_x(void)       { return mx;     }
int mouse_y(void)       { return my;     }
int mouse_buttons(void) { return mbtn;   }
int mouse_moved(void)   { int d = mdirty; mdirty = 0; return d; }
