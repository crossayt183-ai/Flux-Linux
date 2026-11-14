#include "idt.h"
#include "gdt.h"
#include "io.h"
#include "cpu.h"
#include "serial.h"
#include "string.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

typedef struct PACKED {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} idt_entry_t;

typedef struct PACKED {
    uint16_t limit;
    uint64_t base;
} idt_ptr_t;

static idt_entry_t   idt[256];
static irq_handler_t handlers[256];

extern uint64_t isr_stub_table[49];
extern void isr_128(void);

static void idt_set_gate(uint8_t vec, uint64_t handler, uint8_t type_attr, uint8_t ist) {
    idt[vec].offset_low  = (uint16_t)(handler & 0xFFFF);
    idt[vec].selector    = GDT_KERNEL_CODE;
    idt[vec].ist         = ist;
    idt[vec].type_attr   = type_attr;
    idt[vec].offset_mid  = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[vec].offset_high = (uint32_t)((handler >> 32) & 0xFFFFFFFF);
    idt[vec].zero        = 0;
}

static void pic_remap(void) {
    outb(PIC1_CMD, 0x11); io_wait();
    outb(PIC2_CMD, 0x11); io_wait();
    outb(PIC1_DATA, 0x20); io_wait(); /* IRQ 0..7  -> 32..39 */
    outb(PIC2_DATA, 0x28); io_wait(); /* IRQ 8..15 -> 40..47 */
    outb(PIC1_DATA, 0x04); io_wait();
    outb(PIC2_DATA, 0x02); io_wait();
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();

    /* Mask all initially except cascade IRQ2 */
    outb(PIC1_DATA, 0xFB);
    outb(PIC2_DATA, 0xFF);
}

void pic_mask_irq(uint8_t irq, bool masked) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t bit = (uint8_t)(irq & 7);
    uint8_t val = inb(port);
    if (masked) val |= (uint8_t)(1U << bit);
    else        val &= (uint8_t)~(1U << bit);
    outb(port, val);
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_CMD, 0x20);
    }
    outb(PIC1_CMD, 0x20);
}

void idt_init(void) {
    memset(idt, 0, sizeof(idt));
    memset(handlers, 0, sizeof(handlers));
    pic_remap();

    for (uint8_t i = 0; i < 49; i++) {
        uint8_t ist = (i == 8) ? 1 : 0; /* #DF uses IST1 */
        idt_set_gate(i, isr_stub_table[i], 0x8E, ist);
    }
    /* User-callable software interrupt gate for syscalls (DPL=3) */
    idt_set_gate(128, (uint64_t)(uintptr_t)isr_128, 0xEE, 0);

    idt_ptr_t idtr = {
        .limit = (uint16_t)(sizeof(idt) - 1),
        .base  = (uint64_t)(uintptr_t)&idt
    };
    __asm__ volatile ("lidt %0" : : "m"(idtr));
}

void irq_register(uint8_t vector, irq_handler_t handler) {
    handlers[vector] = handler;
}

static const char *const exc_names[32] = {
    "Division Error (#DE)", "Debug (#DB)", "NMI", "Breakpoint (#BP)",
    "Overflow (#OF)", "Bound Range (#BR)", "Invalid Opcode (#UD)", "Device NA (#NM)",
    "Double Fault (#DF)", "Coproc Overrun", "Invalid TSS (#TS)", "Segment NP (#NP)",
    "Stack Fault (#SS)", "General Protection (#GP)", "Page Fault (#PF)", "Reserved",
    "x87 FPU (#MF)", "Alignment Check (#AC)", "Machine Check (#MC)", "SIMD FP (#XM)",
    "Virtualization (#VE)", "Control Protection (#CP)", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor (#HV)", "VMM Comm (#VC)", "Security (#SX)", "Reserved"
};

cpu_regs_t *isr_dispatch(cpu_regs_t *regs) {
    if (handlers[regs->int_no]) {
        cpu_regs_t *next = handlers[regs->int_no](regs);
        if (regs->int_no > 32 && regs->int_no < 48) {
            pic_send_eoi((uint8_t)(regs->int_no - 32));
        }
        return next ? next : regs;
    }

    if (regs->int_no >= 32 && regs->int_no < 48) {
        pic_send_eoi((uint8_t)(regs->int_no - 32));
        return regs;
    }

    const char *name = (regs->int_no < 32) ? exc_names[regs->int_no] : "Unknown";
    kpanic("Unhandled Exception %lu (%s)\n"
           "  ERR=%lx CR2=%lx RIP=%lx CS=%lx RFLAGS=%lx RSP=%lx",
           regs->int_no, name, regs->err_code, read_cr2(),
           regs->rip, regs->cs, regs->rflags, regs->rsp);
}
