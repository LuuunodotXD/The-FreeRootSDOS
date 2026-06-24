#include "pci.h"
#include "io.h"

#define PCI_ADDR  0xCF8
#define PCI_DATA  0xCFC

static uint32_t pci_addr(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    return (1U << 31) | ((uint32_t)bus << 16) | ((uint32_t)(slot & 0x1F) << 11)
         | ((uint32_t)(func & 0x07) << 8) | (off & 0xFC);
}

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    outl(PCI_ADDR, pci_addr(bus, slot, func, off));
    return inl(PCI_DATA);
}

uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    return (uint16_t)(pci_read32(bus, slot, func, off) >> ((off & 2) * 8));
}

void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t val) {
    outl(PCI_ADDR, pci_addr(bus, slot, func, off));
    outl(PCI_DATA, val);
}

void pci_enable_busmaster(uint8_t bus, uint8_t slot, uint8_t func) {
    uint32_t cmd = pci_read32(bus, slot, func, 0x04);
    cmd |= 0x05;  // I/O Enable + Bus Master Enable
    pci_write32(bus, slot, func, 0x04, cmd);
}

int pci_find_device(uint16_t vendor, uint16_t device,
                    uint8_t *bus_out, uint8_t *slot_out, uint8_t *func_out) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint32_t id = pci_read32((uint8_t)bus, slot, 0, 0x00);
            if ((uint16_t)(id & 0xFFFF) != vendor)  continue;
            if ((uint16_t)(id >> 16)    != device)  continue;
            *bus_out  = (uint8_t)bus;
            *slot_out = slot;
            *func_out = 0;
            return 1;
        }
    }
    return 0;
}
