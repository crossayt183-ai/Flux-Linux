#ifndef FLUX_AHCI_H
#define FLUX_AHCI_H

#include "types.h"

#define AHCI_SECTOR_SIZE 512

bool        ahci_init(void);
void        ahci_attach_ramdisk(void *base, uint64_t size_bytes);
void        ahci_enable_ramdisk_fallback(bool enable);
bool        ahci_is_ready(void);
int         ahci_drive_count(void);
bool        ahci_select_drive(int idx);
uint64_t    ahci_sector_count(void);
const char *ahci_model(void);

int ahci_read_sectors(uint64_t lba, uint32_t count, void *buf);
int ahci_write_sectors(uint64_t lba, uint32_t count, const void *buf);

#endif
