#ifndef FLUX_PCIE_QUIRKS_H
#define FLUX_PCIE_QUIRKS_H

#include "types.h"

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t subvendor_id;
    uint16_t subdevice_id;
    uint32_t quirk_flags;
    const char *board_or_chip;
    const char *bug_description;
    const char *stability_patch;
} pcie_board_quirk_t;

typedef struct {
    uint32_t quirks_applied;
    bool     p67_b3_sata_fix;
    bool     vbox_piix4_ide_fix;
    bool     aspm_l1_glitch_guard;
    char     detected_board[64];
} pcie_quirk_status_t;

void   pcie_quirks_init(void);
bool   pcie_quirks_get_status(pcie_quirk_status_t *out);
size_t pcie_quirks_db_size(void);
bool   pcie_quirks_selftest(void);

#endif
