// pcnet.c — Driver AMD PCnet-PCI II (Am79C970A)
//
// PCI ID: vendor 0x1022, device 0x2000
// Modo: DWIO (I/O 32-bit) + SWSTYLE=2 (descriptors 32-bit)
//
// Descriptor rings: 4 RX, 2 TX — buffers estáticos em BSS.
// Virtual == físico sem paginação, igual ao RTL8139.
// Operação por polling puro (interrupts mascarados no CSR3).

#include "pcnet.h"
#include "netdev.h"
#include "pci.h"
#include "io.h"
#include <stdint.h>

// ----------------------------------------------------------------
// Portas DWIO (base + offset)
// ----------------------------------------------------------------
#define PCNET_RDP    0x10   // Register Data Port  (32-bit)
#define PCNET_RAP    0x14   // Register Address Port (32-bit)
#define PCNET_RESET  0x18   // Reset — leitura de 32 bits dispara reset
#define PCNET_BDP    0x1C   // BCR Data Port (32-bit)

// ----------------------------------------------------------------
// CSRs
// ----------------------------------------------------------------
#define CSR0   0    // Status / Control
#define CSR1   1    // Init Block address low
#define CSR2   2    // Init Block address high
#define CSR3   3    // Interrupt Masks (1 = mascara o bit)
#define CSR4   4    // Features / Auto-pad
#define CSR15  15   // Mode

// CSR0 bits
#define CSR0_INIT  0x0001   // lança inicialização pelo Init Block
#define CSR0_STRT  0x0002   // inicia operação (RX/TX)
#define CSR0_STOP  0x0004   // para a placa
#define CSR0_IDON  0x0100   // Init Done — limpa escrevendo 1
#define CSR0_RINT  0x0400   // RX Interrupt — limpa escrevendo 1
#define CSR0_TINT  0x0200   // TX Interrupt — limpa escrevendo 1

// ----------------------------------------------------------------
// BCRs
// ----------------------------------------------------------------
#define BCR20  20   // Software Style

// ----------------------------------------------------------------
// Descriptor status bits
// ----------------------------------------------------------------
#define DESC_OWN  0x8000   // 1 = card owns; 0 = software owns
#define DESC_STP  0x0200   // Start of Packet
#define DESC_ENP  0x0100   // End of Packet

// ----------------------------------------------------------------
// Layout de memória
// ----------------------------------------------------------------
#define RX_COUNT  4      // descriptors RX (2^2 = 4)
#define TX_COUNT  2      // descriptors TX (2^1 = 2)
#define BUF_SIZE  1544   // tamanho de cada buffer (~1 frame Ethernet + folga)

// ----------------------------------------------------------------
// Descriptor RX — SWSTYLE=2, 16 bytes
// ----------------------------------------------------------------
typedef struct {
    uint32_t base;      // endereço físico do buffer de recepção
    int16_t  bcnt;      // 2's complement do tamanho do buffer (negativo)
    int16_t  status;    // flags (OWN, STP, ENP, ERR...)
    uint32_t msg_len;   // bytes recebidos (bits 11:0, inclui FCS de 4 bytes)
    uint32_t reserved;
} __attribute__((packed, aligned(16))) RxDesc;

// Descriptor TX — SWSTYLE=2, 16 bytes
typedef struct {
    uint32_t base;      // endereço físico do buffer de transmissão
    int16_t  length;    // 2's complement do tamanho do pacote (negativo)
    int16_t  status;    // flags (OWN, STP, ENP...)
    uint32_t misc;      // erros de TX (preenchido pela placa)
    uint32_t reserved;
} __attribute__((packed, aligned(16))) TxDesc;

// Init Block — SWSTYLE=2, 28 bytes
typedef struct {
    uint16_t mode;       // 0x0000 = modo normal
    uint8_t  rlen;       // bits 7:4 = log2(RX_COUNT), bits 3:0 = 0
    uint8_t  tlen;       // bits 7:4 = log2(TX_COUNT), bits 3:0 = 0
    uint8_t  mac[6];     // endereço MAC
    uint16_t reserved;
    uint32_t ladrf[2];   // filtro multicast lógico (0 = rejeita tudo)
    uint32_t rdra;       // endereço físico do ring RX
    uint32_t tdra;       // endereço físico do ring TX
} __attribute__((packed, aligned(4))) InitBlock;

// ----------------------------------------------------------------
// Buffers estáticos — virtual == físico sem paginação
// ----------------------------------------------------------------
static RxDesc   rx_ring[RX_COUNT];
static TxDesc   tx_ring[TX_COUNT];
static uint8_t  rx_buf [RX_COUNT][BUF_SIZE];
static uint8_t  tx_buf [TX_COUNT][BUF_SIZE];
static InitBlock init_block;

// ----------------------------------------------------------------
// Estado do driver
// ----------------------------------------------------------------
static uint16_t iobase  = 0;
static uint8_t  mac[6];
static int      rx_head = 0;   // próximo RX descriptor a verificar
static int      tx_head = 0;   // próximo TX descriptor livre
static int      ready   = 0;
static void   (*rx_cb)(const uint8_t *, uint16_t) = 0;

// ----------------------------------------------------------------
// Acesso indireto via RAP/RDP e RAP/BDP (DWIO)
// ----------------------------------------------------------------
static void csr_write(uint32_t idx, uint32_t val) {
    outl(iobase + PCNET_RAP, idx);
    outl(iobase + PCNET_RDP, val);
}
static uint32_t csr_read(uint32_t idx) {
    outl(iobase + PCNET_RAP, idx);
    return inl(iobase + PCNET_RDP);
}
static void bcr_write(uint32_t idx, uint32_t val) {
    outl(iobase + PCNET_RAP, idx);
    outl(iobase + PCNET_BDP, val);
}

// ----------------------------------------------------------------
// Inicialização
// ----------------------------------------------------------------
int pcnet_init(void) {
    uint8_t bus, slot, func;
    if (!pci_find_device(0x1022, 0x2000, &bus, &slot, &func)) return -1;

    uint32_t bar0 = pci_read32(bus, slot, func, 0x10);
    if (!(bar0 & 1)) return -1;    // BAR0 precisa ser I/O space
    iobase = (uint16_t)(bar0 & 0xFFFC);

    pci_enable_busmaster(bus, slot, func);

    // Dispara reset via leitura de 32 bits na porta RESET
    inl(iobase + PCNET_RESET);
    for (int i = 0; i < 1000; i++) inb(iobase);  // delay ~breve

    // Entra em modo DWIO: escrever 0 como 32 bits no RDP
    // (antes disto o chip está em WIO; esta escrita é segura em ambos)
    outl(iobase + PCNET_RDP, 0);

    // BCR20 = 2: SWSTYLE=2 — descritores e I/O em 32 bits
    bcr_write(BCR20, 2);

    // Lê MAC do APROM (bytes 0-5 do espaço de I/O)
    for (int i = 0; i < 6; i++)
        mac[i] = inb(iobase + i);

    // ---- Monta ring RX ----
    // Todos os descriptors pertencem à placa inicialmente (OWN=1)
    for (int i = 0; i < RX_COUNT; i++) {
        rx_ring[i].base    = (uint32_t)(uintptr_t)rx_buf[i];
        rx_ring[i].bcnt    = (int16_t)(-(int)BUF_SIZE);   // negativo
        rx_ring[i].status  = (int16_t)DESC_OWN;
        rx_ring[i].msg_len = 0;
        rx_ring[i].reserved = 0;
    }

    // ---- Monta ring TX ----
    // Todos os descriptors pertencem ao software inicialmente (OWN=0)
    for (int i = 0; i < TX_COUNT; i++) {
        tx_ring[i].base    = (uint32_t)(uintptr_t)tx_buf[i];
        tx_ring[i].length  = 0;
        tx_ring[i].status  = 0;
        tx_ring[i].misc    = 0;
        tx_ring[i].reserved = 0;
    }

    // ---- Monta Init Block ----
    // rlen: bits 7:4 = log2(RX_COUNT) = log2(4) = 2  →  2 << 4 = 0x20
    // tlen: bits 7:4 = log2(TX_COUNT) = log2(2) = 1  →  1 << 4 = 0x10
    init_block.mode     = 0x0000;
    init_block.rlen     = (2 << 4);
    init_block.tlen     = (1 << 4);
    init_block.reserved = 0;
    init_block.ladrf[0] = 0;    // rejeita todo multicast
    init_block.ladrf[1] = 0;
    init_block.rdra     = (uint32_t)(uintptr_t)rx_ring;
    init_block.tdra     = (uint32_t)(uintptr_t)tx_ring;
    for (int i = 0; i < 6; i++)
        init_block.mac[i] = mac[i];

    // Aponta CSR1/CSR2 para o Init Block
    uint32_t ib = (uint32_t)(uintptr_t)&init_block;
    csr_write(CSR1, ib & 0xFFFF);
    csr_write(CSR2, ib >> 16);

    // Mascara todas as interrupções — vamos usar polling
    csr_write(CSR3, 0x5F40);

    // Habilita auto-pad em TX (frames menores que 64 bytes)
    csr_write(CSR4, csr_read(CSR4) | 0x0800);

    // Dispara INIT e aguarda IDON
    csr_write(CSR0, CSR0_INIT);
    int timeout = 200000;
    while (timeout-- && !(csr_read(CSR0) & CSR0_IDON));
    if (timeout <= 0) { iobase = 0; return -1; }

    // Limpa IDON e inicia operação (STRT)
    csr_write(CSR0, CSR0_STRT | CSR0_IDON);

    rx_head = 0;
    tx_head = 0;
    ready   = 1;
    return 0;
}

int  pcnet_present(void) { return ready; }

void pcnet_get_mac(uint8_t out[6]) {
    for (int i = 0; i < 6; i++) out[i] = mac[i];
}

void pcnet_set_rx_callback(void (*cb)(const uint8_t *, uint16_t)) {
    rx_cb = cb;
}

// ----------------------------------------------------------------
// Transmissão
// ----------------------------------------------------------------
int pcnet_send(const uint8_t *data, uint16_t len) {
    if (!ready || !len || len > BUF_SIZE) return -1;

    // Aguarda descriptor TX livre (OWN=0 = software)
    int timeout = 100000;
    while ((tx_ring[tx_head].status & DESC_OWN) && timeout--);
    if (timeout <= 0) return -1;

    for (int i = 0; i < len; i++)
        tx_buf[tx_head][i] = data[i];

    // length deve ser negativo; bits 15:12 = 0xF (spec PCnet)
    tx_ring[tx_head].length = (int16_t)((-len) | 0xF000);
    tx_ring[tx_head].misc   = 0;
    // STP+ENP: frame inteiro num único descriptor; OWN entrega à placa
    tx_ring[tx_head].status = (int16_t)(DESC_OWN | DESC_STP | DESC_ENP);

    // TDMD (Transmit Demand): acorda a placa
    csr_write(CSR0, CSR0_STRT);

    // Aguarda conclusão (OWN=0 = placa terminou)
    timeout = 100000;
    while ((tx_ring[tx_head].status & DESC_OWN) && timeout--);

    // Limpa flag de TX no CSR0
    csr_write(CSR0, CSR0_TINT);

    tx_head = (tx_head + 1) % TX_COUNT;
    return 0;
}

// ----------------------------------------------------------------
// Recepção por polling
// ----------------------------------------------------------------
void pcnet_poll(void) {
    if (!ready) return;

    for (int guard = 0; guard < RX_COUNT; guard++) {
        RxDesc *d = &rx_ring[rx_head];

        // OWN=1 significa que a placa ainda não preencheu este descriptor
        if (d->status & (int16_t)DESC_OWN) break;

        // Verifica frame completo e sem erro (STP+ENP setados pela placa)
        int complete = (d->status & (int16_t)(DESC_STP | DESC_ENP))
                       == (int16_t)(DESC_STP | DESC_ENP);

        if (complete) {
            // msg_len inclui 4 bytes de FCS — subtrai para entregar payload
            uint16_t raw = (uint16_t)(d->msg_len & 0x0FFF);
            uint16_t len = (raw > 4) ? (uint16_t)(raw - 4) : 0;

            if (len >= 14 && len <= BUF_SIZE && rx_cb)
                rx_cb(rx_buf[rx_head], len);
        }

        // Devolve descriptor à placa
        d->msg_len = 0;
        d->status  = (int16_t)DESC_OWN;

        rx_head = (rx_head + 1) % RX_COUNT;
    }

    // Limpa flags de interrupção pendentes
    uint32_t s = csr_read(CSR0);
    if (s & (CSR0_RINT | CSR0_TINT))
        csr_write(CSR0, s & (CSR0_RINT | CSR0_TINT));
}

// ----------------------------------------------------------------
// Registro no HAL de rede
// ----------------------------------------------------------------
static NetDriver pcnet_drv = {
    "PCnet-PCI II",
    pcnet_init,   pcnet_present,
    pcnet_send,   pcnet_poll,
    pcnet_get_mac, pcnet_set_rx_callback,
};

void pcnet_register(void) { netdev_register(&pcnet_drv); }
