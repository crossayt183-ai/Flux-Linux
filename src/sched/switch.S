.intel_syntax noprefix
.global context_switch
.global task_kthread_trampoline
.global user_iret_trampoline

.section .text

/* void context_switch(uint64_t *old_rsp (rdi), uint64_t new_rsp (rsi)) */
context_switch:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [rdi], rsp
    mov rsp, rsi
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

/* Popped into by fresh kernel thread: r12 = fn, r13 = arg */
task_kthread_trampoline:
    sti
    mov rdi, r13
    call r12
    xor edi, edi
    .extern task_exit
    call task_exit

/* Popped into by fresh user thread or forked child: rsp points to cpu_regs_t */
user_iret_trampoline:
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
