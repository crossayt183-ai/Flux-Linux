.intel_syntax noprefix
.extern isr_dispatch
.global isr_stub_table

.section .text

.macro ISR_NOERR num
.global isr_\num
isr_\num:
    push 0
    push \num
    jmp isr_common
.endm

.macro ISR_ERR num
.global isr_\num
isr_\num:
    push \num
    jmp isr_common
.endm

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

/* Hardware IRQs (32..47) + Software vectors (48, 128) */
.altmacro
.set i, 32
.rept 17
    ISR_NOERR %i
    .set i, i + 1
.endr

ISR_NOERR 128

isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    cld
    call isr_dispatch
    mov rsp, rax

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16
    iretq

.section .rodata
.macro STUB_PTR num
    .quad isr_\num
.endm

isr_stub_table:
.set j, 0
.rept 49
    STUB_PTR %j
    .set j, j + 1
.endr
