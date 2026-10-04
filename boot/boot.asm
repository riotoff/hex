[org 0x7C00]
[bits 16]

KERNEL_LOAD_ADDR equ 0x10000
KERNEL_SECTORS   equ 512

E820_MAP_ADDR    equ 0x5000

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    mov si, msg_loading
    call print16

    in al, 0x92
    or al, 2
    out 0x92, al

    mov dword [E820_MAP_ADDR], 0
    mov dword [E820_MAP_ADDR + 4], 0
    xor ebx, ebx
    mov di, E820_MAP_ADDR + 8
.e820_loop:
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 24
    int 0x15
    jc .e820_done
    cmp eax, 0x534D4150
    jne .e820_done
    inc dword [E820_MAP_ADDR]
    add di, 24
    test ebx, ebx
    jnz .e820_loop
.e820_done:

    mov word [dap_count], 1
    mov word [dap_offset], 0
    mov word [dap_segment], 0x1000
    mov dword [dap_lba], 1
    mov dword [dap_lba + 4], 0
    mov word [sectors_left], KERNEL_SECTORS
.read_loop:
    mov ah, 0x42
    mov dl, [boot_drive]
    mov si, dap
    int 0x13
    jc .read_error
    add word [dap_segment], 0x20
    add dword [dap_lba], 1
    adc dword [dap_lba + 4], 0
    dec word [sectors_left]
    jnz .read_loop

    cli
    lgdt [gdt_ptr]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x18:pm_entry

.read_error:
    mov si, msg_error
    call print16
    hlt
    jmp .read_error

print16:
    lodsb
    test al, al
    jz .done
    push si
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    pop si
    jmp print16
.done:
    ret

[bits 32]
pm_entry:
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x7C00

    mov edi, 0x1000
    xor eax, eax
    mov ecx, 3 * 1024
    rep stosd

    mov dword [0x1000], 0x2007
    mov dword [0x2000], 0x3007

    mov edi, 0x3000
    mov eax, 0x87
    mov ecx, 512
.fill_pd:
    mov [edi], eax
    add edi, 8
    add eax, 0x200000
    dec ecx
    jnz .fill_pd

    mov eax, 0x1000
    mov cr3, eax

    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    jmp 0x08:lm_entry

[bits 64]
lm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov rsp, 0x2000000
    mov rbp, rsp
    mov rax, KERNEL_LOAD_ADDR
    jmp rax

    cli
.hang:
    hlt
    jmp .hang

boot_drive:    db 0
sectors_left:  dw 0
msg_loading:   db "Loading Hex...", 13, 10, 0
msg_error:     db "Disk read error!", 13, 10, 0

align 4
dap:
    db 0x10
    db 0
dap_count:     dw 1
dap_offset:    dw 0
dap_segment:   dw 0x1000
dap_lba:       dq 1

align 8
gdt:
    dq 0x0000000000000000
gdt_code64:
    dq 0x00AF9A000000FFFF
gdt_data64:
    dq 0x00CF92000000FFFF
gdt_code32:
    dq 0x00CF9A000000FFFF
gdt_data32:
    dq 0x00CF92000000FFFF
gdt_end:
gdt_ptr:
    dw gdt_end - gdt - 1
    dd gdt

times 510 - ($ - $$) db 0
dw 0xAA55
