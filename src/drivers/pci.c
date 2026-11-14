#include "pci.h"
#include "io.h"
#include "serial.h"

#define PCI_CFG_ADDR 0x0CF8
#define PCI_CFG_DATA 0x0CFC
#define MAX_PCI_DEVS 32

static pci_device_t pci_devs[MAX_PCI_DEVS];
static size_t       pci_dev_count = 0;

static inline uint32_t pci_addr(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    return (1U << 31) |
           ((uint32_t)bus  << 16) |
           ((uint32_t)(slot & 0x1F) << 11) |
           ((uint32_t)(func & 0x07) << 8) |
           ((uint32_t)(off & 0xFC));
}

uint32_t pci_cfg_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    outl(PCI_CFG_ADDR, pci_addr(bus, slot, func, offset));
    return inl(PCI_CFG_DATA);
}

void pci_cfg_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    outl(PCI_CFG_ADDR, pci_addr(bus, slot, func, offset));
    outl(PCI_CFG_DATA, val);
}

uint16_t pci_cfg_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t v = pci_cfg_read32(bus, slot, func, offset & 0xFC);
    return (uint16_t)((v >> ((offset & 2) * 8)) & 0xFFFFU);
}

void pci_cfg_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t val) {
    uint32_t v = pci_cfg_read32(bus, slot, func, offset & 0xFC);
    uint32_t shift = (uint32_t)(offset & 2) * 8U;
    v = (v & ~(0xFFFFU << shift)) | ((uint32_t)val << shift);
    pci_cfg_write32(bus, slot, func, offset & 0xFC, v);
}

static void probe_func(uint8_t bus, uint8_t slot, uint8_t func) {
    uint32_t id = pci_cfg_read32(bus, slot, func, 0x00);
    uint16_t vendor = (uint16_t)(id & 0xFFFFU);
    if (vendor == 0xFFFFU) return;

    if (pci_dev_count >= MAX_PCI_DEVS) return;
    pci_device_t *d = &pci_devs[pci_dev_count++];
    d->bus       = bus;
    d->slot      = slot;
    d->func      = func;
    d->vendor_id = vendor;
    d->device_id = (uint16_t)(id >> 16);

    uint32_t cls = pci_cfg_read32(bus, slot, func, 0x08);
    d->class_code = (uint8_t)(cls >> 24);
    d->subclass   = (uint8_t)(cls >> 16);
    d->prog_if    = (uint8_t)(cls >> 8);
    d->irq_line   = (uint8_t)(pci_cfg_read32(bus, slot, func, 0x3C) & 0xFFU);

    for (int b = 0; b < 6; b++) {
        d->bars[b] = pci_cfg_read32(bus, slot, func, (uint8_t)(0x10 + b * 4));
    }
}

void pci_init(void) {
    pci_dev_count = 0;
    for (uint8_t bus = 0; bus < 4; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint32_t id = pci_cfg_read32(bus, slot, 0, 0x00);
            if ((id & 0xFFFFU) == 0xFFFFU) continue;
            uint32_t hdr = pci_cfg_read32(bus, slot, 0, 0x0C);
            bool multi = ((hdr >> 16) & 0x80U) != 0;
            probe_func(bus, slot, 0);
            if (multi) {
                for (uint8_t fn = 1; fn < 8; fn++) {
                    probe_func(bus, slot, fn);
                }
            }
        }
    }
    kprintf("[PCI] Enumerated %lu PCI devices:\n", pci_dev_count);
    for (size_t i = 0; i < pci_dev_count; i++) {
        pci_device_t *d = &pci_devs[i];
        kprintf("  %02x:%02x.%u vendor=0x%04x dev=0x%04x class=%02x.%02x\n",
                d->bus, d->slot, d->func, d->vendor_id, d->device_id, d->class_code, d->subclass);
    }
}

void pci_enable_bus_master(const pci_device_t *dev) {
    if (!dev) return;
    uint16_t cmd = pci_cfg_read16(dev->bus, dev->slot, dev->func, 0x04);
    cmd |= 0x0007U; /* I/O + Memory + Bus Master */
    pci_cfg_write16(dev->bus, dev->slot, dev->func, 0x04, cmd);
}

bool pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t *out) {
    for (size_t i = 0; i < pci_dev_count; i++) {
        if (pci_devs[i].vendor_id == vendor_id && pci_devs[i].device_id == device_id) {
            if (out) *out = pci_devs[i];
            return true;
        }
    }
    return false;
}

bool pci_find_class(uint8_t class_code, uint8_t subclass, int prog_if, pci_device_t *out) {
    for (size_t i = 0; i < pci_dev_count; i++) {
        if (pci_devs[i].class_code == class_code && pci_devs[i].subclass == subclass &&
            (prog_if < 0 || pci_devs[i].prog_if == (uint8_t)prog_if)) {
            if (out) *out = pci_devs[i];
            return true;
        }
    }
    return false;
}

uint64_t pci_get_bar_addr(const pci_device_t *dev, int bar_idx, bool *is_io, bool *is_64) {
    if (!dev || bar_idx < 0 || bar_idx >= 6) return 0;
    uint32_t low = dev->bars[bar_idx];
    if (low & 1U) {
        if (is_io) *is_io = true;
        if (is_64) *is_64 = false;
        return (uint64_t)(low & ~0x3U);
    }
    if (is_io) *is_io = false;
    uint32_t type = (low >> 1) & 0x3U;
    if (type == 2 && bar_idx + 1 < 6) {
        if (is_64) *is_64 = true;
        uint64_t hi = dev->bars[bar_idx + 1];
        return (hi << 32) | (uint64_t)(low & ~0xFULL);
    }
    if (is_64) *is_64 = false;
    return (uint64_t)(low & ~0xFULL);
}

const char *pci_class_name(uint8_t class_code, uint8_t subclass) {
    if (class_code == 0x01 && subclass == 0x06) return "SATA AHCI";
    if (class_code == 0x01 && subclass == 0x08) return "NVMe";
    if (class_code == 0x01) return "Storage";
    if (class_code == 0x02 && subclass == 0x00) return "Ethernet";
    if (class_code == 0x03) return "Display/VGA";
    if (class_code == 0x04 && subclass == 0x01) return "AC97 Audio";
    if (class_code == 0x06 && subclass == 0x00) return "Host Bridge";
    if (class_code == 0x06 && subclass == 0x01) return "ISA Bridge";
    if (class_code == 0x0C && subclass == 0x05) return "SMBus";
    return "PCI Device";
}

size_t pci_list_devices(pci_device_t *out, size_t max_count) {
    size_t n = MIN(pci_dev_count, max_count);
    for (size_t i = 0; i < n; i++) out[i] = pci_devs[i];
    return n;
}
