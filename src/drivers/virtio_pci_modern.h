#ifndef FLUX_VIRTIO_PCI_MODERN_H
#define FLUX_VIRTIO_PCI_MODERN_H

#include "types.h"

typedef struct {
    uint32_t rule_id;
    uint16_t hw_class;
    uint16_t param_val;
    const char *profile_key;
    const char *fastpath_engine;
    const char *telemetry_bound;
} virtio_pci_modern_rule_t;

void   virtio_pci_modern_init(void);
size_t virtio_pci_modern_db_size(void);
bool   virtio_pci_modern_selftest(void);

#endif
