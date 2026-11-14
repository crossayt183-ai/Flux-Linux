#ifndef FLUX_TIMER_H
#define FLUX_TIMER_H

#include "types.h"

#define TIMER_HZ 100U

void     timer_init(uint32_t hz);
uint64_t timer_ticks(void);
uint64_t timer_uptime_ms(void);
void     timer_sleep_ms(uint64_t ms);

typedef void (*timer_tick_hook_t)(void);
void timer_set_tick_hook(timer_tick_hook_t hook);

#endif
