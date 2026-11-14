#ifndef FLUX_PCI_H
#define FLUX_PCI_H

#include "types.h"

typedef struct {
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  irq_line;
    uint32_t bars[6];
} pci_device_t;

void     pci_init(void);
uint32_t pci_cfg_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void     pci_cfg_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);
uint16_t pci_cfg_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void     pci_cfg_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t val);

void        pci_enable_bus_master(const pci_device_t *dev);
bool        pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t *out);
bool        pci_find_class(uint8_t class_code, uint8_t subclass, int prog_if, pci_device_t *out);
uint64_t    pci_get_bar_addr(const pci_device_t *dev, int bar_idx, bool *is_io, bool *is_64);
const char *pci_class_name(uint8_t class_code, uint8_t subclass);
size_t      pci_list_devices(pci_device_t *out, size_t max_count);

#endif
