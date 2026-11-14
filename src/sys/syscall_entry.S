.intel_syntax noprefix
.extern syscall_dispatch
.global syscall_fast_entry

.section .text

/*
 * Fast SYSCALL entry from Ring 3:
 *   RAX = syscall number
 *   RDI, RSI, RDX, R10, R8, R9 = args 1..6
 *   RCX = user RIP, R11 = user RFLAGS
 * IA32_FMASK ensures IF=0 on entry.
 * IA32_KERNEL_GS_BASE points to syscall_cpu_ctx:
 *   [gs:0] = current task's kstack_top
 *   [gs:8] = temporary scratch for user RSP while IF=0
 */
syscall_fast_entry:
    swapgs
    mov qword ptr gs:[8], rsp
    mov rsp, qword ptr gs:[0]

    /* Synthesize full cpu_regs_t frame so fork/exec/yield work identically to int 0x80 */
    push 0x1B                   /* ss = GDT_USER_DATA | 3 */
    push qword ptr gs:[8]       /* user rsp */
    swapgs                      /* immediately restore KERNEL_GS_BASE while IF=0 */

    push r11                    /* rflags */
    push 0x23                   /* cs = GDT_USER_CODE | 3 */
    push rcx                    /* rip */
    push 0                      /* err_code */
    push 128                    /* int_no */

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
    sti                         /* safe now: stack & GS are clean kernel state */
    call syscall_dispatch
    cli
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
    add rsp, 16                 /* skip int_no, err_code */
    iretq
