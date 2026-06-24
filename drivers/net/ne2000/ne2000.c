#include "ne2000.h"
#include "netdev.h"
#include "io.h"
#include <stdint.h>
#include "isa.h"

// ---- Registradores DP8390 (offsets do I/O base) ----
#define NE_CR        0x00   // Command Register (todas as páginas)
#define NE_PSTART    0x01   // (write, page0)
#define NE_PSTOP     0x02   // (write, page0)
#define NE_BNRY      0x03   // Boundary Pointer (page0)
#define NE_TPSR      0x04   // Transmit Page Start (write, page0)
#define NE_TBCR0     0x05
#define NE_TBCR1     0x06
#define NE_ISR       0x07   // Interrupt Status (todas as páginas)
#define NE_RSAR0     0x08   // Remote Start Address (page0)
#define NE_RSAR1     0x09
#define NE_RBCR0     0x0A
#define NE_RBCR1     0x0B
#define NE_RCR       0x0C   // Receive Config (write, page0)
#define NE_TCR       0x0D   // Transmit Config (write, page0)
#define NE_DCR       0x0E   // Data Config (write, page0)
#define NE_IMR       0x0F   // Interrupt Mask (write, page0)

#define NE_CURR      0x07   // Current Page (page1)
#define NE_PAR0      0x01   // Physical Address (page1, 6 bytes)

#define NE_DATAPORT  0x10   // porta de dados p/ remote DMA
#define NE_RESET     0x1F   // porta de reset

// ---- Bits do Command Register ----
#define CR_STP   0x01
#define CR_STA   0x02
#define CR_TXP   0x04
#define CR_RD0   0x08
#define CR_RD1   0x10
#define CR_RD2   0x20
#define CR_PS0   0x40

// ---- Bits do ISR ----
#define ISR_PRX  0x01   // pacote recebido intacto
#define ISR_PTX  0x02   // pacote transmitido
#define ISR_RDC  0x40   // remote DMA completa
#define ISR_RST  0x80   // reset completo

// ---- Layout de memória do NIC (mesmos valores usados pelo driver ne.c do Linux) ----
#define TX_START_PG  0x40
#define TX_PAGES     6           // 6*256 = 1536 bytes, cobre 1 frame Ethernet
#define RX_START_PG  (TX_START_PG + TX_PAGES)   // 0x46
#define RX_STOP_PG   0x80

static uint16_t iobase   = 0;
static uint8_t  mac[6];
static uint8_t  next_pkt;        // próxima página a ler do ring
static int      ready    = 0;
static void   (*rx_cb)(const uint8_t *, uint16_t) = 0;

// ---- Portas candidatas para sondagem (ISA não tem auto-detecção) ----
static const uint16_t candidate_ports[] = { 0x300, 0x320, 0x340, 0x360, 0x280, 0x250, 0x240, 0x350 };

static int probe_port(uint16_t base) {
    // Reset: lê a porta de reset e escreve o mesmo valor de volta
    uint8_t rst = inb(base + NE_RESET);
    outb(base + NE_RESET, rst);

    // Aguarda ISR.RST setar (até ~5000 iterações, sem depender de timer)
    int timeout = 5000;
    while (timeout--) {
        if (inb(base + NE_ISR) & ISR_RST) break;
    }
    if (timeout <= 0) return 0;

    outb(base + NE_CR, CR_STP | CR_RD2);   // stop, aborta DMA, page0

    // Lê a PROM (32 bytes, modo palavra) e confere assinatura NE2000
    // bytes 14 e 15 da PROM == 0x57 ('W') em cartões NE2000 genuínos
    outb(base + NE_DCR, 0x49);             // word-wide, normal, FIFO=4
    outb(base + NE_RBCR0, 32);
    outb(base + NE_RBCR1, 0);
    outb(base + NE_RCR,  0x20);            // monitor mode (descarta RX)
    outb(base + NE_TCR,  0x02);            // loopback interno
    outb(base + NE_RSAR0, 0);
    outb(base + NE_RSAR1, 0);
    outb(base + NE_CR, CR_STA | CR_RD0);   // start + remote read

    uint16_t prom[16];
    for (int i = 0; i < 16; i++)
        prom[i] = inw(base + NE_DATAPORT);

    if ((prom[14] & 0xFF) != 0x57 || (prom[15] & 0xFF) != 0x57)
        return 0;   // não é NE2000

    // Guarda o MAC (byte baixo de cada palavra, primeiras 6)
    for (int i = 0; i < 6; i++)
        mac[i] = (uint8_t)(prom[i] & 0xFF);

    return 1;
}

int ne2000_init(void) {
    iobase = isa_probe_ports(candidate_ports,
                             sizeof(candidate_ports)/sizeof(candidate_ports[0]),
                             probe_port);
    if (!iobase) { ready = 0; return -1; }

    // ---- Inicialização completa ----
    outb(iobase + NE_CR, CR_STP | CR_RD2);   // stop, aborta DMA, page0
    outb(iobase + NE_DCR, 0x49);             // word-wide
    outb(iobase + NE_RBCR0, 0);
    outb(iobase + NE_RBCR1, 0);
    outb(iobase + NE_RCR, 0x20);             // monitor mode (ainda sem aceitar pacotes)
    outb(iobase + NE_TCR, 0x02);             // loopback interno (evita lixo na rede)

    outb(iobase + NE_PSTART, RX_START_PG);
    outb(iobase + NE_PSTOP,  RX_STOP_PG);
    outb(iobase + NE_BNRY,   RX_START_PG);
    outb(iobase + NE_TPSR,   TX_START_PG);

    outb(iobase + NE_ISR, 0xFF);             // limpa todos os flags de interrupção
    outb(iobase + NE_IMR, 0x00);             // mascara tudo — vamos só fazer polling

    // Página 1: grava o MAC (PAR0-5) e o ponteiro CURR
    outb(iobase + NE_CR, CR_STP | CR_PS0);   // stop, page1
    for (int i = 0; i < 6; i++)
        outb(iobase + NE_PAR0 + i, mac[i]);
    outb(iobase + NE_CURR, RX_START_PG + 1);
    next_pkt = RX_START_PG + 1;

    // Volta para page0, modo normal, e liga a placa
    outb(iobase + NE_CR, CR_STP | CR_RD2);   // page0, stop
    outb(iobase + NE_RCR, 0x04);             // aceita pacotes para nosso endereço (AB=broadcast off por enquanto)
    outb(iobase + NE_TCR, 0x00);             // desliga loopback — modo normal
    outb(iobase + NE_CR, CR_STA | CR_RD2);   // start, page0

    ready = 1;
    return 0;
}

int  ne2000_present(void) { return ready; }
void ne2000_get_mac(uint8_t out[6]) { for (int i=0;i<6;i++) out[i]=mac[i]; }
void ne2000_set_rx_callback(void (*cb)(const uint8_t *, uint16_t)) { rx_cb = cb; }

// ---- Espera um bit do ISR setar, com timeout ----
static int wait_isr(uint8_t bit, int timeout) {
    while (timeout--) {
        if (inb(iobase + NE_ISR) & bit) return 1;
    }
    return 0;
}

int ne2000_send(const uint8_t *data, uint16_t len) {
    if (!ready) return -1;
    if (len < 60) len = 60;        // tamanho mínimo de frame Ethernet
    if (len > 1514) return -1;

    // Aguarda DMA anterior estar livre
    outb(iobase + NE_CR, CR_STA | CR_RD2);

    // Remote DMA write: copia o pacote para a memória do NIC
    outb(iobase + NE_ISR, ISR_RDC);              // limpa flag antiga
    outb(iobase + NE_RBCR0, (uint8_t)(len & 0xFF));
    outb(iobase + NE_RBCR1, (uint8_t)(len >> 8));
    outb(iobase + NE_RSAR0, 0x00);
    outb(iobase + NE_RSAR1, TX_START_PG);
    outb(iobase + NE_CR, CR_STA | CR_RD1);        // start + remote write

    uint16_t words = (uint16_t)((len + 1) / 2);
    for (uint16_t i = 0; i < words; i++) {
        uint16_t lo = data[i*2];
        uint16_t hi = (i*2 + 1 < len) ? data[i*2 + 1] : 0;
        outw(iobase + NE_DATAPORT, (uint16_t)(lo | (hi << 8)));
    }

    if (!wait_isr(ISR_RDC, 20000)) return -1;
    outb(iobase + NE_ISR, ISR_RDC);

    // Dispara a transmissão
    outb(iobase + NE_TPSR, TX_START_PG);
    outb(iobase + NE_TBCR0, (uint8_t)(len & 0xFF));
    outb(iobase + NE_TBCR1, (uint8_t)(len >> 8));
    outb(iobase + NE_CR, CR_STA | CR_TXP);        // start + transmite

    if (!wait_isr(ISR_PTX, 50000)) return -1;
    outb(iobase + NE_ISR, ISR_PTX);

    return 0;
}

void ne2000_poll(void) {
    if (!ready) return;

    static uint8_t pkt_buf[1600];

    for (int guard = 0; guard < 8; guard++) {     // limite por chamada, evita travar em flood
        // CURR fica na page1
        outb(iobase + NE_CR, CR_STA | CR_PS0);
        uint8_t curr = inb(iobase + NE_CURR);
        outb(iobase + NE_CR, CR_STA | CR_RD2);    // volta para page0

        if (next_pkt == curr) break;              // nada novo

        // Lê o cabeçalho de 4 bytes do pacote (status, next_ptr, len_lo, len_hi)
        outb(iobase + NE_ISR, ISR_RDC);
        outb(iobase + NE_RBCR0, 4);
        outb(iobase + NE_RBCR1, 0);
        outb(iobase + NE_RSAR0, 0x00);
        outb(iobase + NE_RSAR1, next_pkt);
        outb(iobase + NE_CR, CR_STA | CR_RD0);

        uint16_t w0 = inw(iobase + NE_DATAPORT);
        uint16_t w1 = inw(iobase + NE_DATAPORT);
        wait_isr(ISR_RDC, 5000);
        outb(iobase + NE_ISR, ISR_RDC);

        uint8_t  status   = (uint8_t)(w0 & 0xFF);
        uint8_t  next_ptr = (uint8_t)(w0 >> 8);
        uint16_t pkt_len  = w1;          // inclui os 4 bytes do cabeçalho

        if (pkt_len < 4 || pkt_len > 1518 + 4) { next_pkt = curr; break; } // sanidade

        uint16_t eth_len = (uint16_t)(pkt_len - 4);

        if ((status & ISR_PRX) && rx_cb && eth_len <= sizeof(pkt_buf)) {
            // Lê o frame em si (a partir de next_pkt, offset +4)
            outb(iobase + NE_ISR, ISR_RDC);
            outb(iobase + NE_RBCR0, (uint8_t)(eth_len & 0xFF));
            outb(iobase + NE_RBCR1, (uint8_t)(eth_len >> 8));
            outb(iobase + NE_RSAR0, 0x04);
            outb(iobase + NE_RSAR1, next_pkt);
            outb(iobase + NE_CR, CR_STA | CR_RD0);

            uint16_t words = (uint16_t)((eth_len + 1) / 2);
            for (uint16_t i = 0; i < words; i++) {
                uint16_t w = inw(iobase + NE_DATAPORT);
                pkt_buf[i*2] = (uint8_t)(w & 0xFF);
                if (i*2 + 1 < eth_len) pkt_buf[i*2+1] = (uint8_t)(w >> 8);
            }
            wait_isr(ISR_RDC, 5000);
            outb(iobase + NE_ISR, ISR_RDC);

            rx_cb(pkt_buf, eth_len);
        }

        // Avança o ponteiro de boundary
        next_pkt = next_ptr;
        uint8_t bnry = (uint8_t)(next_ptr == RX_START_PG ? RX_STOP_PG - 1 : next_ptr - 1);
        outb(iobase + NE_BNRY, bnry);
    }
}

// ---- Registro no HAL de rede ----
static NetDriver ne2000_drv = {
    "NE2000 (ISA)",
    ne2000_init, ne2000_present,
    ne2000_send, ne2000_poll,
    ne2000_get_mac, ne2000_set_rx_callback,
};
void ne2000_register(void) { netdev_register(&ne2000_drv); }
