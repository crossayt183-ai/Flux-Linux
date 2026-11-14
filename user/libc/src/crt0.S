.intel_syntax noprefix
.section .text
.global _start
.extern main

_start:
    mov rdi, [rsp]
    lea rsi, [rsp + 8]
    call main
    mov rdi, rax
    mov rax, 0
    syscall
1:
    jmp 1b
