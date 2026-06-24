// e1000.c — Driver Intel E1000 (82540EM)
//
// PCI ID: vendor 0x8086, device 0x100E  (82540EM — padrão do QEMU "-device e1000")
//         vendor 0x8086, device 0x100F  (82545EM — variante comum)
//
// Diferenças em relação ao RTL8139/PCnet:
//   - BAR0 é MMIO (memória mapeada), não I/O ports
//   - Descriptor rings com campos de endereço de 64 bits (upper 32 = 0)
//   - Descriptor rings precisam de multiplos de 128 bytes → mínimo 8 entries
//   - MAC lida da EEPROM via registrador EERD
//
// Virtual == físico sem paginação: ponteiro MMIO e buffers em BSS funcionam direto.
// Polling puro — todas as interrupções mascaradas no IMC.

#include "e1000.h"
#include "netdev.h"
#include "pci.h"
#include <stdint.h>

// ----------------------------------------------------------------
// Registradores (offsets MMIO)
// ----------------------------------------------------------------
#define E1000_CTRL   0x0000   // Device Control
#define E1000_STATUS 0x0008   // Device Status
#define E1000_EERD   0x0014   // EEPROM Read
#define E1000_ICR    0x00C0   // Interrupt Cause Read (limpa na leitura)
#define E1000_IMC    0x00D8   // Interrupt Mask Clear
#define E1000_RCTL   0x0100   // Receive Control
#define E1000_TCTL   0x0400   // Transmit Control
#define E1000_TIPG   0x0410   // TX Inter-Packet Gap
#define E1000_RDBAL  0x2800   // RX Descriptor Base Address Low
#define E1000_RDBAH  0x2804   // RX Descriptor Base Address High
#define E1000_RDLEN  0x2808   // RX Descriptor Ring Length (bytes)
#define E1000_RDH    0x2810   // RX Descriptor Head
#define E1000_RDT    0x2818   // RX Descriptor Tail
#define E1000_TDBAL  0x3800   // TX Descriptor Base Address Low
#define E1000_TDBAH  0x3804   // TX Descriptor Base Address High
#define E1000_TDLEN  0x3808   // TX Descriptor Ring Length (bytes)
#define E1000_TDH    0x3810   // TX Descriptor Head
#define E1000_TDT    0x3818   // TX Descriptor Tail
#define E1000_MTA    0x5200   // Multicast Table Array (128 × uint32)
#define E1000_RAL0   0x5400   // Receive Address Low  (MAC bytes 3:0)
#define E1000_RAH0   0x5404   // Receive Address High (MAC bytes 5:4 + AV)

// CTRL bits
#define CTRL_SLU   0x00000040   // Set Link Up
#define CTRL_RST   0x04000000   // Software Reset (self-clearing)

// RCTL bits
#define RCTL_EN    0x00000002   // Receiver Enable
#define RCTL_BAM   0x00008000   // Broadcast Accept Mode
#define RCTL_SECRC 0x04000000   // Strip Ethernet CRC (não inclui FCS no buffer)
// bits 17:16 = 00 → buffer size 2048 bytes (default, não precisa setar)

// TCTL bits
#define TCTL_EN    0x00000002   // Transmit Enable
#define TCTL_PSP   0x00000008   // Pad Short Packets

// TIPG: IPGT=8, IPGR1=8, IPGR2=6 (IEEE 802.3 full-duplex, 82540EM)
#define TIPG_VAL   0x00602008

// EERD bits (82540EM)
#define EERD_START 0x00000001   // bit 0: inicia leitura
#define EERD_DONE  0x00000010   // bit 4: leitura concluída

// RAH bits
#define RAH_AV     0x80000000   // Address Valid

// Descriptor status bits
#define RDESC_DD   0x01   // RX Done
#define RDESC_EOP  0x02   // End of Packet

#define TDESC_EOP  0x01   // TX: end of packet
#define TDESC_IFCS 0x02   // TX: insert FCS/CRC
#define TDESC_RS   0x08   // TX: report status (seta DD quando concluído)
#define TDESC_DD   0x01   // TX status: descriptor done

// ----------------------------------------------------------------
// Layout de memória
// ----------------------------------------------------------------
// RDLEN/TDLEN precisam ser múltiplos de 128 bytes → mínimo 8 descriptors (8×16=128)
#define RX_COUNT  8
#define TX_COUNT  8
#define BUF_SIZE  2048   // casa com RCTL default (bits 17:16 = 00)

// Descriptor RX — 16 bytes, campos de endereço 64-bit
typedef struct {
    uint64_t addr;       // endereço físico do buffer (upper 32 = 0)
    uint16_t length;     // bytes recebidos (preenchido pela placa)
    uint16_t checksum;   // checksum do pacote
    uint8_t  status;     // flags DD, EOP...
    uint8_t  errors;     // flags de erro
    uint16_t special;    // VLAN tag etc.
} __attribute__((packed, aligned(16))) E1000RxDesc;

// Descriptor TX — 16 bytes
typedef struct {
    uint64_t addr;       // endereço físico do buffer
    uint16_t length;     // tamanho do pacote
    uint8_t  cso;        // checksum offset (0 = não usa)
    uint8_t  cmd;        // bits EOP, IFCS, RS...
    uint8_t  status;     // bit DD setado pela placa quando concluído
    uint8_t  css;        // checksum start (0 = não usa)
    uint16_t special;
} __attribute__((packed, aligned(16))) E1000TxDesc;

// ----------------------------------------------------------------
// Buffers estáticos (virtual == físico sem paginação)
// ----------------------------------------------------------------
static E1000RxDesc rx_ring[RX_COUNT] __attribute__((aligned(16)));
static E1000TxDesc tx_ring[TX_COUNT] __attribute__((aligned(16)));
static uint8_t     rx_buf[RX_COUNT][BUF_SIZE];
static uint8_t     tx_buf[TX_COUNT][BUF_SIZE];

// ----------------------------------------------------------------
// Estado do driver
// ----------------------------------------------------------------
static volatile uint32_t *mmio    = 0;   // base MMIO mapeada
static uint8_t            mac[6];
static int                rx_head = 0;   // próximo RX a verificar
static int                tx_head = 0;   // próximo TX descriptor livre
static int                ready   = 0;
static void (*rx_cb)(const uint8_t *, uint16_t) = 0;

// ----------------------------------------------------------------
// Acesso MMIO (índice em uint32 → offset / 4)
// ----------------------------------------------------------------
static uint32_t e_read(uint32_t reg) {
    return mmio[reg >> 2];
}
static void e_write(uint32_t reg, uint32_t val) {
    mmio[reg >> 2] = val;
}

// ----------------------------------------------------------------
// Leitura da EEPROM via EERD (82540EM)
// addr: word address (0=MAC[1:0], 1=MAC[3:2], 2=MAC[5:4])
// ----------------------------------------------------------------
static int eeprom_read(uint8_t addr, uint16_t *out) {
    e_write(E1000_EERD, EERD_START | ((uint32_t)addr << 8));
    int timeout = 200000;
    uint32_t val;
    do {
        val = e_read(E1000_EERD);
        if (val & EERD_DONE) { *out = (uint16_t)(val >> 16); return 0; }
    } while (timeout--);
    return -1;   // timeout — EEPROM não respondeu
}

// ----------------------------------------------------------------
// Inicialização
// ----------------------------------------------------------------
int e1000_init(void) {
    uint8_t bus, slot, func;

    // Tenta 82540EM primeiro, depois 82545EM
    if (!pci_find_device(0x8086, 0x100E, &bus, &slot, &func) &&
        !pci_find_device(0x8086, 0x100F, &bus, &slot, &func))
        return -1;

    // BAR0: MMIO space (bit 0 = 0, bits 2:1 = 00 → 32-bit)
    uint32_t bar0 = pci_read32(bus, slot, func, 0x10);
    if (bar0 & 1) return -1;   // seria I/O, não esperado
    mmio = (volatile uint32_t *)(uintptr_t)(bar0 & 0xFFFFFFF0);

    pci_enable_busmaster(bus, slot, func);

    // Software reset — aguarda o bit RST se limpar sozinho
    e_write(E1000_CTRL, e_read(E1000_CTRL) | CTRL_RST);
    for (int i = 0; i < 10000; i++)
        if (!(e_read(E1000_CTRL) & CTRL_RST)) break;

    // Mascara todas as interrupções e limpa pendências
    e_write(E1000_IMC, 0xFFFFFFFF);
    (void)e_read(E1000_ICR);

    // Set Link Up (necessário em alguns ambientes)
    e_write(E1000_CTRL, e_read(E1000_CTRL) | CTRL_SLU);

    // Lê MAC da EEPROM (3 words, little-endian)
    uint16_t w0, w1, w2;
    if (eeprom_read(0, &w0) || eeprom_read(1, &w1) || eeprom_read(2, &w2)) {
        mmio = 0; return -1;
    }
    mac[0] = (uint8_t)(w0 & 0xFF);  mac[1] = (uint8_t)(w0 >> 8);
    mac[2] = (uint8_t)(w1 & 0xFF);  mac[3] = (uint8_t)(w1 >> 8);
    mac[4] = (uint8_t)(w2 & 0xFF);  mac[5] = (uint8_t)(w2 >> 8);

    // Programar filtro de endereço MAC (RAL0/RAH0)
    uint32_t ral = (uint32_t)mac[0]        | ((uint32_t)mac[1] << 8)
                 | ((uint32_t)mac[2] << 16) | ((uint32_t)mac[3] << 24);
    uint32_t rah = (uint32_t)mac[4] | ((uint32_t)mac[5] << 8) | RAH_AV;
    e_write(E1000_RAL0, ral);
    e_write(E1000_RAH0, rah);

    // Zera tabela multicast (128 entradas × 4 bytes = 512 bytes)
    for (int i = 0; i < 128; i++)
        mmio[(E1000_MTA >> 2) + i] = 0;

    // ---- Ring RX ----
    for (int i = 0; i < RX_COUNT; i++) {
        rx_ring[i].addr   = (uint64_t)(uintptr_t)rx_buf[i];
        rx_ring[i].status = 0;
    }
    e_write(E1000_RDBAL, (uint32_t)(uintptr_t)rx_ring);
    e_write(E1000_RDBAH, 0);
    e_write(E1000_RDLEN, RX_COUNT * 16);   // 8×16 = 128 bytes ✓
    e_write(E1000_RDH,   0);
    e_write(E1000_RDT,   RX_COUNT - 1);   // todos os descriptors pertencem à placa

    // ---- Ring TX ----
    for (int i = 0; i < TX_COUNT; i++) {
        tx_ring[i].addr   = (uint64_t)(uintptr_t)tx_buf[i];
        tx_ring[i].status = TDESC_DD;   // começa como "done" → livre para uso
    }
    e_write(E1000_TDBAL, (uint32_t)(uintptr_t)tx_ring);
    e_write(E1000_TDBAH, 0);
    e_write(E1000_TDLEN, TX_COUNT * 16);
    e_write(E1000_TDH,   0);
    e_write(E1000_TDT,   0);

    // ---- Controle de transmissão ----
    // CT=0x0F (collision threshold), COLD=0x040 (full-duplex distance)
    e_write(E1000_TCTL, TCTL_EN | TCTL_PSP | (0x0F << 4) | (0x040 << 12));
    e_write(E1000_TIPG, TIPG_VAL);

    // ---- Controle de recepção ----
    // SECRC: strip CRC → length entregue não inclui os 4 bytes do FCS
    e_write(E1000_RCTL, RCTL_EN | RCTL_BAM | RCTL_SECRC);

    rx_head = 0;
    tx_head = 0;
    ready   = 1;
    return 0;
}

int  e1000_present(void) { return ready; }

void e1000_get_mac(uint8_t out[6]) {
    for (int i = 0; i < 6; i++) out[i] = mac[i];
}

void e1000_set_rx_callback(void (*cb)(const uint8_t *, uint16_t)) {
    rx_cb = cb;
}

// ----------------------------------------------------------------
// Transmissão
// ----------------------------------------------------------------
int e1000_send(const uint8_t *data, uint16_t len) {
    if (!ready || !len || len > BUF_SIZE) return -1;

    // Aguarda descriptor livre (DD=1 significa que a placa terminou com ele)
    int timeout = 100000;
    while (!(tx_ring[tx_head].status & TDESC_DD) && timeout--);
    if (timeout <= 0) return -1;

    for (int i = 0; i < len; i++)
        tx_buf[tx_head][i] = data[i];

    tx_ring[tx_head].length = len;
    tx_ring[tx_head].cso    = 0;
    tx_ring[tx_head].css    = 0;
    tx_ring[tx_head].status = 0;   // limpa DD antes de entregar à placa
    tx_ring[tx_head].cmd    = TDESC_EOP | TDESC_IFCS | TDESC_RS;

    // Avança TDT para enfileirar o frame
    int next = (tx_head + 1) % TX_COUNT;
    e_write(E1000_TDT, (uint32_t)next);

    // Aguarda conclusão (DD setado pela placa)
    timeout = 100000;
    while (!(tx_ring[tx_head].status & TDESC_DD) && timeout--);

    tx_head = next;
    return 0;
}

// ----------------------------------------------------------------
// Recepção por polling
// ----------------------------------------------------------------
void e1000_poll(void) {
    if (!ready) return;

    for (int guard = 0; guard < RX_COUNT; guard++) {
        E1000RxDesc *d = &rx_ring[rx_head];

        // DD=0 → placa ainda não preencheu este descriptor
        if (!(d->status & RDESC_DD)) break;

        uint16_t len = d->length;

        // SECRC ativo: CRC já foi removido pela placa
        // Verifica EOP e tamanho mínimo de um frame Ethernet
        if ((d->status & RDESC_EOP) && len >= 14 && len <= BUF_SIZE && rx_cb)
            rx_cb(rx_buf[rx_head], len);

        // Devolve descriptor à placa: limpa status e avança RDT
        d->status = 0;
        e_write(E1000_RDT, (uint32_t)rx_head);

        rx_head = (rx_head + 1) % RX_COUNT;
    }

    // Limpa flags de interrupção pendentes
    (void)e_read(E1000_ICR);
}

// ----------------------------------------------------------------
// Registro no HAL de rede
// ----------------------------------------------------------------
static NetDriver e1000_drv = {
    "E1000",
    e1000_init,   e1000_present,
    e1000_send,   e1000_poll,
    e1000_get_mac, e1000_set_rx_callback,
};

void e1000_register(void) { netdev_register(&e1000_drv); }
