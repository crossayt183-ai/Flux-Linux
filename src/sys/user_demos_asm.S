.intel_syntax noprefix

/*
 * Self-contained position-independent Ring 3 user programs.
 * Copied to 0x0000000000400000 in clean user address spaces and run at CPL=3.
 */

.global user_selftest_start
.global user_selftest_end
.global user_worker_start
.global user_worker_end
.global user_beeper_start
.global user_beeper_end

.section .rodata

/* ---------------- Program 1: /bin/selftest ----------------
 * Exercises in Ring 3:
 *  1) SYS_WRITE (fd 1) via SYSCALL instruction
 *  2) SYS_OPEN("/etc/motd") + SYS_READ + SYS_CLOSE
 *  3) SYS_BRK heap expansion + lazy #PF demand-zero write
 *  4) SYS_MMAP + lazy #PF allocation at 0x20000000
 *  5) SYS_PIPE + SYS_FORK + Copy-on-Write isolation check + SYS_WAITPID
 *  6) int 0x80 (SYS_GETPID + SYS_SLEEP_MS)
 *  7) SYS_EXECVE("/bin/worker") -> replaces image & exits cleanly
 */
.align 16
user_selftest_start:
    /* write(1, msg_boot, len) */
    mov rax, 1
    mov rdi, 1
    lea rsi, [rip + msg_boot]
    lea rdx, [rip + msg_boot_end]
    sub rdx, rsi
    syscall

    /* open("/etc/motd", 0) -> read -> close */
    mov rax, 3
    lea rdi, [rip + path_motd]
    xor rsi, rsi
    syscall
    cmp rax, 0
    jl .fail
    mov r12, rax                /* r12 = fd */

    sub rsp, 128
    mov rax, 2                  /* SYS_READ */
    mov rdi, r12
    mov rsi, rsp
    mov rdx, 127
    syscall
    cmp rax, 0
    jle .fail

    mov rdx, rax                /* print motd bytes */
    mov rax, 1
    mov rdi, 1
    mov rsi, rsp
    syscall

    mov rax, 4                  /* SYS_CLOSE */
    mov rdi, r12
    syscall
    add rsp, 128

    /* SYS_BRK(0) then grow +8192 and write to trigger lazy #PF */
    mov rax, 12
    xor rdi, rdi
    syscall
    mov r12, rax                /* old brk */
    lea rdi, [rax + 8192]
    mov rax, 12
    syscall
    cmp rax, r12
    jle .fail
    mov rbx, 0xCAFEBABE12345678
    mov qword ptr [r12], rbx
    mov qword ptr [r12 + 4096], rbx
    cmp qword ptr [r12 + 4096], rbx
    jne .fail

    /* SYS_MMAP(0x40000000, 8192, PROT_R|W, MAP_ANON) -> lazy #PF */
    mov rax, 13
    mov rdi, 0x40000000
    mov rsi, 8192
    mov rdx, 3
    mov r10, 0x22
    syscall
    cmp rax, 0
    jl .fail
    mov r13, rax                /* r13 = mapped COW test page */
    mov rax, 0x1122334455667788
    mov qword ptr [r13], rax

    /* SYS_PIPE(pipefd[2]) */
    sub rsp, 16
    mov rax, 15
    mov rdi, rsp
    syscall
    cmp rax, 0
    jl .fail
    mov r14d, dword ptr [rsp]     /* r14 = read_fd */
    mov r15d, dword ptr [rsp + 4] /* r15 = write_fd */

    /* SYS_FORK() */
    mov rax, 7
    syscall
    cmp rax, 0
    jl .fail
    test rax, rax
    jnz .parent

.child:
    /* Write to COW page in child; MUST NOT modify parent's copy! */
    mov rax, 0xDEAD0002BEEF0002
    mov qword ptr [r13], rax

    /* Close read end */
    mov rax, 4
    mov rdi, r14
    syscall

    /* Send "COW_OK\n" over pipe to parent */
    mov rax, 1
    mov rdi, r15
    lea rsi, [rip + msg_pipe]
    lea rdx, [rip + msg_pipe_end]
    sub rdx, rsi
    syscall

    /* Close write end & exit(42) */
    mov rax, 4
    mov rdi, r15
    syscall
    mov rax, 0
    mov rdi, 42
    syscall

.parent:
    mov rbx, rax                /* rbx = child pid */

    /* Close write end in parent */
    mov rax, 4
    mov rdi, r15
    syscall

    /* Read 7 bytes from pipe */
    sub rsp, 32
    mov rax, 2
    mov rdi, r14
    mov rsi, rsp
    mov rdx, 7
    syscall
    cmp rax, 7
    jne .fail
    mov rax, 4
    mov rdi, r14
    syscall
    add rsp, 32

    /* Wait for child & check exit_code == 42 */
    sub rsp, 16
    mov rax, 9
    mov rdi, rbx
    mov rsi, rsp
    syscall
    cmp rax, rbx
    jne .fail
    mov eax, dword ptr [rsp]
    add rsp, 16
    cmp eax, 42
    jne .fail

    /* Verify COW isolation: parent's [r13] must still equal 0x1122334455667788 */
    mov rax, 0x1122334455667788
    cmp qword ptr [r13], rax
    jne .fail

    /* Clean up mmap region + print pass banner */
    mov rax, 14                 /* SYS_MUNMAP */
    mov rdi, r13
    mov rsi, 8192
    syscall
    add rsp, 16

    /* Test int 0x80 entry (SYS_SLEEP_MS = 11 for 20 ms) */
    mov rax, 11
    mov rdi, 20
    int 0x80

    mov rax, 1
    mov rdi, 1
    lea rsi, [rip + msg_cow_ok]
    lea rdx, [rip + msg_cow_ok_end]
    sub rdx, rsi
    syscall

    /* Replace current image via SYS_EXECVE("/bin/worker") */
    mov rax, 8
    lea rdi, [rip + path_worker]
    xor rsi, rsi
    xor rdx, rdx
    syscall

.fail:
    mov rax, 1
    mov rdi, 1
    lea rsi, [rip + msg_fail]
    lea rdx, [rip + msg_fail_end]
    sub rdx, rsi
    syscall
    mov rax, 0
    mov rdi, 1
    syscall

msg_boot:
    .ascii "[USER/Ring3] Self-test started via SYSCALL\n"
msg_boot_end:
path_motd:
    .asciz "/etc/motd"
msg_pipe:
    .ascii "COW_OK\n"
msg_pipe_end:
msg_cow_ok:
    .ascii "[USER/Ring3] Lazy #PF + fork COW + pipe + int 0x80 OK!\n"
msg_cow_ok_end:
path_worker:
    .asciz "/bin/worker"
msg_fail:
    .ascii "[USER/Ring3] TEST FAILED!\n"
msg_fail_end:
user_selftest_end:

/* ---------------- Program 2: /bin/worker ---------------- */
.align 16
user_worker_start:
    mov rax, 1
    mov rdi, 1
    lea rsi, [rip + worker_msg]
    lea rdx, [rip + worker_msg_end]
    sub rdx, rsi
    syscall

    mov rax, 0                  /* SYS_EXIT(0) */
    xor rdi, rdi
    syscall

worker_msg:
    .ascii "[USER/Ring3] execve('/bin/worker') live, exiting 0\n"
worker_msg_end:
user_worker_end:

/* ---------------- Program 3: /bin/beeper ---------------- */
.align 16
user_beeper_start:
    mov rax, 24                 /* SYS_AUDIO_NOTE(523Hz, 120ms, triangle=3, vol=80) */
    mov rdi, 523
    mov rsi, 120
    mov rdx, 3
    mov r10, 80
    syscall

    mov rax, 11                 /* SYS_SLEEP_MS(130) */
    mov rdi, 130
    syscall

    mov rax, 24                 /* C6 = 659 Hz */
    mov rdi, 659
    mov rsi, 160
    mov rdx, 3
    mov r10, 80
    syscall

    mov rax, 0
    xor rdi, rdi
    syscall
user_beeper_end:
