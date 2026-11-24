#ifndef FLUX_USB_XHCI_THUNDERBOLT_H
#define FLUX_USB_XHCI_THUNDERBOLT_H

#include "types.h"

typedef struct {
    uint32_t rule_id;
    uint16_t hw_class;
    uint16_t param_val;
    const char *profile_key;
    const char *fastpath_engine;
    const char *telemetry_bound;
} usb_xhci_thunderbolt_rule_t;

void   usb_xhci_thunderbolt_init(void);
size_t usb_xhci_thunderbolt_db_size(void);
bool   usb_xhci_thunderbolt_selftest(void);

#endif
