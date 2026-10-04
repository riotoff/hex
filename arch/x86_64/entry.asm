[bits 64]
[extern kernel_main]
[extern __bss_start]
[extern __bss_end]
global _start
section .text

_start:
    cld
    mov rdi, __bss_start
    mov rcx, __bss_end
    sub rcx, rdi
    xor al, al
    rep stosb

    call kernel_main

    cli
.hang:
    hlt
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite progbits
