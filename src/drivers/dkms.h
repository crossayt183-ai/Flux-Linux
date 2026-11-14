#ifndef FLUX_DKMS_H
#define FLUX_DKMS_H

#include "types.h"

typedef enum {
    DKMS_STATE_UNINSTALLED = 0,
    DKMS_STATE_BUILT       = 1,
    DKMS_STATE_SIGNED_MOK  = 2,
    DKMS_STATE_LOADED      = 3
} dkms_state_t;

typedef struct {
    const char  *pkg_name;
    const char  *kmod_name;
    const char  *version;
    const char  *vendor_target;
    const char  *license;
    const char  *summary;
    uint16_t     pci_vendor;
    uint16_t     pci_device;
    uint32_t     code_size_kb;
    bool         proprietary;
} dkms_pkg_spec_t;

typedef struct {
    char         pkg_name[32];
    char         kmod_name[24];
    char         version[16];
    char         vendor_target[24];
    char         summary[64];
    dkms_state_t state;
    bool         proprietary;
    bool         mok_signed;
    uint8_t      sha256_sig[32];
    uint32_t     build_time_ms;
} dkms_module_status_t;

void   dkms_init(void);
size_t dkms_module_count(void);
bool   dkms_get_module(size_t idx, dkms_module_status_t *out);
int    dkms_find_module(const char *name_or_kmod);
bool   dkms_build_and_load(const char *name_or_kmod);
bool   dkms_remove_module(const char *name_or_kmod);
size_t dkms_hw_quirk_count(void);
bool   dkms_selftest(void);

#endif
