.intel_syntax noprefix
.global gdt_flush
.global tss_flush

.section .text
gdt_flush:
    lgdt [rdi]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    pop rcx
    push 0x08
    push rcx
    retfq

tss_flush:
    ltr di
    ret
