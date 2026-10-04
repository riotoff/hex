#include "elf.h"
#include "serial.h"

static void mem_copy_u8(uint8_t* d, const uint8_t* s, uint64_t n) {
    void* dst = d;
    const void* src = s;
    uint64_t cnt = n;
    __asm__ volatile (
        "cld\n"
        "rep movsb\n"
        : "+D"(dst), "+S"(src), "+c"(cnt)
        :
        : "memory"
    );
}

static void mem_zero_u8(uint8_t* d, uint64_t n) {
    void* dst = d;
    uint64_t cnt = n;
    __asm__ volatile (
        "cld\n"
        "xor %%al, %%al\n"
        "rep stosb\n"
        : "+D"(dst), "+c"(cnt)
        :
        : "rax", "memory"
    );
}

int elf_load(const void* data, size_t size, elf_info_t* out) {
    if (!data || !out) return -1;
    if (size < sizeof(elf64_ehdr_t)) return -2;

    const uint8_t* raw = (const uint8_t*)data;
    const elf64_ehdr_t* hdr = (const elf64_ehdr_t*)data;

    if (hdr->e_ident[0] != 0x7F || hdr->e_ident[1] != 'E' ||
        hdr->e_ident[2] != 'L'  || hdr->e_ident[3] != 'F') return -3;
    if (hdr->e_ident[4] != ELFCLASS64) return -4;
    if (hdr->e_machine  != EM_X86_64)  return -5;
    if (hdr->e_type     != ET_EXEC)    return -6;

    uint64_t max_end = 0;

    for (uint16_t i = 0; i < hdr->e_phnum; i++) {
        uint64_t ph_off = hdr->e_phoff + (uint64_t)i * hdr->e_phentsize;
        if (ph_off + sizeof(elf64_phdr_t) > size) return -7;

        const elf64_phdr_t* ph = (const elf64_phdr_t*)(raw + ph_off);
        if (ph->p_type != PT_LOAD) continue;

        if (ph->p_offset + ph->p_filesz > size) return -8;

        uint8_t* dst = (uint8_t*)(uintptr_t)ph->p_vaddr;

        if (ph->p_filesz > 0)
            mem_copy_u8(dst, raw + ph->p_offset, ph->p_filesz);
        if (ph->p_memsz > ph->p_filesz)
            mem_zero_u8(dst + ph->p_filesz, ph->p_memsz - ph->p_filesz);

        uint64_t seg_end = ph->p_vaddr + ph->p_memsz;
        if (seg_end > max_end) max_end = seg_end;
    }

    out->entry    = hdr->e_entry;
    out->load_end = max_end;
    return 0;
}
