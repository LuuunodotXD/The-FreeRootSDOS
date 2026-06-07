#include "rtl8139.h"
#include "pci.h"
#include "io.h"
#include <stdint.h>

// Registradores
#define RTL_IDR0     0x00
#define RTL_TSD0     0x10
#define RTL_TSAD0    0x20
#define RTL_RBSTART  0x30
#define RTL_CR       0x37
#define RTL_CAPR     0x38
#define RTL_IMR      0x3C
#define RTL_ISR      0x3E
#define RTL_TCR      0x40
#define RTL_RCR      0x44
#define RTL_CONFIG1  0x52

#define CR_RST   0x10
#define CR_RE    0x08
#define CR_TE    0x04
#define CR_BUFE  0x01

#define ISR_ROK   0x0001
#define ISR_TOK   0x0004
#define ISR_RXOVW 0x0010

#define RX_BUF_SIZE  8192
#define TX_BUFS      4
#define TX_BUF_SIZE  1536

// Buffers DMA em BSS (virtual == físico sem paginação)
static uint8_t rx_buf[RX_BUF_SIZE + 16 + 1500];
static uint8_t tx_buf[TX_BUFS][TX_BUF_SIZE];

// Estado do driver
static uint16_t       iobase = 0;
static uint8_t        mac[6];
static uint8_t        tx_idx = 0;
static uint32_t       rx_pos = 0;
static int            ready  = 0;
static rtl8139_rx_cb  rx_cb  = 0;

static void     w8 (uint8_t r, uint8_t  v) { outb (iobase + r, v); }
static void     w16(uint8_t r, uint16_t v) { outw (iobase + r, v); }
static void     w32(uint8_t r, uint32_t v) { outl (iobase + r, v); }
static uint8_t  r8 (uint8_t r)             { return inb (iobase + r); }
static uint32_t r32(uint8_t r)             { return inl (iobase + r); }

int rtl8139_init(void) {
    uint8_t bus, slot, func;
    if (!pci_find_device(0x10EC, 0x8139, &bus, &slot, &func)) return -1;

    // Lê BAR0 — deve ser I/O space (bit 0 = 1)
    uint32_t bar0 = pci_read32(bus, slot, func, 0x10);
    if (!(bar0 & 1)) return -2;
    iobase = (uint16_t)(bar0 & 0xFFFC);

    pci_enable_busmaster(bus, slot, func);

    w8(RTL_CONFIG1, 0x00);          // power on
    w8(RTL_CR, CR_RST);             // software reset
    while (r8(RTL_CR) & CR_RST) {} // aguarda reset

    for (int i = 0; i < 6; i++) mac[i] = r8(RTL_IDR0 + i);

    rx_pos = 0;
    w32(RTL_RBSTART, (uint32_t)(uintptr_t)rx_buf);

    w16(RTL_IMR, 0x0000);
    w32(RTL_TCR, 0x00000600);       // IFG padrão, DMA burst 1024
    w32(RTL_RCR, 0x0000E78E);       // AB|AM|APM, WRAP, RX buf 8KB, sem FIFO thr
    w8 (RTL_CR,  CR_RE | CR_TE);    // habilita RX + TX

    ready = 1;
    return 0;
}

int  rtl8139_present(void)           { return ready; }
void rtl8139_set_rx_callback(rtl8139_rx_cb cb) { rx_cb = cb; }
void rtl8139_get_mac(uint8_t out[6]) { for (int i=0;i<6;i++) out[i]=mac[i]; }

int rtl8139_send(const uint8_t *data, uint16_t len) {
    if (!ready || !len || len > TX_BUF_SIZE) return -1;
    for (int i = 0; i < len; i++) tx_buf[tx_idx][i] = data[i];
    w32(RTL_TSAD0 + tx_idx * 4, (uint32_t)(uintptr_t)tx_buf[tx_idx]);
    w32(RTL_TSD0  + tx_idx * 4, len & 0x1FFF); // limpa OWN, inicia TX
    while (!(r32(RTL_TSD0 + tx_idx * 4) & (1 << 15))) {} // aguarda TOK
    w16(RTL_ISR, ISR_TOK);
    tx_idx = (tx_idx + 1) % TX_BUFS;
    return 0;
}

void rtl8139_poll(void) {
    if (!ready) return;
    static int in_poll = 0;
    if (in_poll) return;
    in_poll = 1;

    while (!(r8(RTL_CR) & CR_BUFE)) {
        uint16_t *hdr     = (uint16_t *)(rx_buf + rx_pos);
        uint16_t  status  = hdr[0];
        uint16_t  pkt_len = hdr[1];
        if (!(status & 0x01) || pkt_len < 4 || pkt_len > 1518) {
            rx_pos = 0;
            w16(RTL_CAPR, (uint16_t)(0 - 16));
            break;
        }
        uint16_t data_len = pkt_len - 4;
        uint32_t end = rx_pos + 4 + data_len;
        if (end <= sizeof(rx_buf) && rx_cb)
            rx_cb(rx_buf + rx_pos + 4, data_len);
        rx_pos = (rx_pos + pkt_len + 4 + 3) & ~3U;
        if (rx_pos >= RX_BUF_SIZE) rx_pos -= RX_BUF_SIZE;
        w16(RTL_CAPR, (uint16_t)(rx_pos - 16));
    }
    w16(RTL_ISR, 0xFFFF);
    in_poll = 0;
}

void rtl8139_irq_handler(void) {
    rtl8139_poll();
    outb(0xA0, 0x20);   // EOI para PIC2
    outb(0x20, 0x20);   // EOI para PIC1
}
