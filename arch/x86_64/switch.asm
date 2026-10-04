[bits 64]

section .text

global __task_switch
; void __task_switch(uint64_t* save_rsp_slot, uint64_t new_rsp)
; rdi = save_rsp_slot
; rsi = new_rsp
__task_switch:
    push rbx
    push rbp
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
    pop rbp
    pop rbx
    ret

global __task_trampoline
extern __task_entry
extern task_exit
__task_trampoline:
    call __task_entry
    call task_exit
    hlt
    jmp __task_trampoline

section .note.GNU-stack noalloc noexec nowrite progbits
