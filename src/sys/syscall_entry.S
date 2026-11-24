.intel_syntax noprefix
.extern syscall_dispatch
.extern g_fast_sysret_count
.global syscall_fast_entry

.section .text

/*
 * Fast SYSCALL/SYSRETQ entry from Ring 3:
 *   RAX = syscall number
 *   RDI, RSI, RDX, R10, R8, R9 = args 1..6
 *   RCX = user RIP, R11 = user RFLAGS
 * IA32_FMASK ensures IF=0, TF=0, DF=0 on entry.
 * IA32_STAR[63:48] = 0x10 -> SYSRETQ loads CS=0x23 (0x10+16|3), SS=0x1B (0x10+8|3).
 * Includes CVE-2014-4699 canonical Ring-3 RIP/RSP check before SYSRETQ.
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
    mov rbx, rsp                /* remember original frame pointer (callee-saved) */
    cld
    sti                         /* safe now: stack & GS are clean kernel state */
    call syscall_dispatch
    cli

    /* Verify fast SYSRETQ eligibility + CVE-2014-4699 canonical address guard */
    cmp rax, rbx                /* frame switched? -> use iretq */
    jne .Liret_fallback
    cmp qword ptr [rax + 144], 0x23
    jne .Liret_fallback
    cmp qword ptr [rax + 168], 0x1B
    jne .Liret_fallback

    /* If execve altered regs->rip away from original rcx, restore full GPRs via iretq */
    mov rcx, qword ptr [rax + 136]
    cmp rcx, qword ptr [rax + 96]
    jne .Liret_fallback

    /* CVE-2014-4699: RCX (user RIP) and user RSP MUST be strictly lower-half canonical (< 2^47) */
    mov rdx, rcx
    shr rdx, 47
    jnz .Liret_fallback
    mov rdx, qword ptr [rax + 160]
    shr rdx, 47
    jnz .Liret_fallback

    lock inc qword ptr [rip + g_fast_sysret_count]

    mov rsp, rax
    pop r15
    pop r14
    pop r13
    pop r12
    add rsp, 8                  /* skip r11 slot (restored from regs->rflags) */
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    add rsp, 8                  /* skip rcx slot (restored from regs->rip) */
    pop rbx
    pop rax                     /* syscall return value */

    mov rcx, qword ptr [rsp + 16] /* user RIP */
    mov r11, qword ptr [rsp + 32] /* user RFLAGS */
    mov rsp, qword ptr [rsp + 40] /* user RSP (safe: IF=0 until sysretq completes) */
    sysretq

.Liret_fallback:
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
