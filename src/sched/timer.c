#include "timer.h"
#include "idt.h"
#include "io.h"
#include "cpu.h"
#include "serial.h"

#define PIT_FREQ 1193182U

static volatile uint64_t g_ticks = 0;
static uint32_t g_hz = TIMER_HZ;
static timer_tick_hook_t g_tick_hook = NULL;

static cpu_regs_t *pit_irq(cpu_regs_t *regs) {
    g_ticks++;
    /* Acknowledge PIC IRQ0 BEFORE scheduler context switch so new tasks keep receiving timer IRQs */
    pic_send_eoi(0);
    if (g_tick_hook) {
        g_tick_hook();
    }
    return regs;
}

void timer_init(uint32_t hz) {
    if (hz == 0) hz = TIMER_HZ;
    g_hz = hz;
    uint32_t divisor = PIT_FREQ / hz;
    if (divisor < 1) divisor = 1;
    if (divisor > 65535) divisor = 65535;

    outb(0x43, 0x36); /* Channel 0, lobyte/hibyte, rate generator */
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));

    irq_register(32, pit_irq);
    pic_mask_irq(0, false);
    kprintf("[TIMER] PIT configured @ %u Hz (IRQ0 unmasked)\n", g_hz);
}

void timer_set_tick_hook(timer_tick_hook_t hook) {
    g_tick_hook = hook;
}

uint64_t timer_ticks(void) {
    return g_ticks;
}

uint64_t timer_uptime_ms(void) {
    return (g_ticks * 1000ULL) / (uint64_t)g_hz;
}

void timer_sleep_ms(uint64_t ms) {
    uint64_t target = timer_uptime_ms() + ms;
    while (timer_uptime_ms() < target) {
        hlt();
    }
}
