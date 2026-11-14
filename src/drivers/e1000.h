#ifndef FLUX_E1000_H
#define FLUX_E1000_H

#include "types.h"

typedef void (*e1000_rx_cb_t)(const uint8_t *frame, size_t len);

bool e1000_init(void);
bool e1000_is_ready(void);
void e1000_get_mac(uint8_t out_mac[6]);
int  e1000_send(const void *frame, size_t len);
void e1000_poll_rx(e1000_rx_cb_t cb);

#endif
