#ifndef FLUX_IDT_H
#define FLUX_IDT_H

#include "types.h"

typedef struct PACKED {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no;
    uint64_t err_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} cpu_regs_t;

typedef cpu_regs_t *(*irq_handler_t)(cpu_regs_t *regs);

void idt_init(void);
void irq_register(uint8_t vector, irq_handler_t handler);
void pic_mask_irq(uint8_t irq, bool masked);
void pic_send_eoi(uint8_t irq);

#endif
