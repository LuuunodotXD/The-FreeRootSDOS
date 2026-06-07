#ifndef PCI_H
#define PCI_H
#include <stdint.h>

uint32_t pci_read32 (uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
uint16_t pci_read16 (uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
void     pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t val);
void     pci_enable_busmaster(uint8_t bus, uint8_t slot, uint8_t func);
int      pci_find_device(uint16_t vendor, uint16_t device,
                         uint8_t *bus_out, uint8_t *slot_out, uint8_t *func_out);
#endif
